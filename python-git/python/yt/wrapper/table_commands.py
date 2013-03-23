import config
import py_wrapper
from common import flatten, require, unlist, update, EMPTY_GENERATOR, parse_bool, is_prefix, get_value, compose, execute_handling_sigint
from errors import YtError
from version import VERSION
from http import read_content, get_host_for_heavy_operation
from table import TablePath, to_table, to_name, prepare_path
from tree_commands import exists, remove, remove_with_empty_dirs, get_attribute, copy, move, mkdir, find_free_subpath, create, get_type
from file_commands import smart_upload_file
from transaction_commands import _make_transactional_request
from transaction import PingableTransaction
from string_iter_io import StringIterIO
from format import RawFormat

import os
import sys
import types
import logger
from cStringIO import StringIO

""" Auxiliary methods """
def _filter_empty_tables(tables):
    filtered = []
    for table in tables:
        if not exists(table.name):
            logger.warning("Warning: input table '%s' does not exist", table.name)
        else:
            filtered.append(table)
    return filtered

def _prepare_source_tables(tables):
    tables = map(to_table, flatten(tables))
    if config.TREAT_UNEXISTING_AS_EMPTY:
        tables = _filter_empty_tables(tables)
    return tables

def _prepare_reduce_by(reduce_by):
    if reduce_by is None:
        if config.USE_YAMR_SORT_REDUCE_COLUMNS:
            reduce_by = ["key"]
        else:
            raise YtError("reduce_by option is required")
    return flatten(reduce_by)

def _prepare_sort_by(sort_by):
    if sort_by is None:
        if config.USE_YAMR_SORT_REDUCE_COLUMNS:
            sort_by = ["key", "subkey"]
        else:
            raise YtError("sort_by option is required")
    return flatten(sort_by)

def _prepare_files(files):
    if files is None:
        return []

    file_paths = []
    for file in flatten(files):
        if config.DO_NOT_UPLOAD_EMPTY_FILES and os.path.getsize(file) == 0:
            continue
        file_paths.append(smart_upload_file(file))
    return file_paths

def _add_output_fd_redirect(binary, dst_count):
    if config.USE_MAPREDUCE_STYLE_DESTINATION_FDS:
        for fd in xrange(3, 3 + dst_count):
            yt_fd = 1 + (fd - 3) * 3
            binary = binary + " %d>/dev/fd/%d" % (fd, yt_fd)
    return binary

def _prepare_formats(format, input_format, output_format):
    if format is None: format = config.DEFAULT_FORMAT
    if input_format is None: input_format = format
    require(input_format is not None,
            YtError("You should specify input format"))
    if output_format is None: output_format = format
    require(output_format is not None,
            YtError("You should specify output format"))
    return input_format, output_format

def _prepare_format(format):
    if format is None: format = config.DEFAULT_FORMAT
    require(format is not None,
            YtError("You should specify format"))
    return format

def _prepare_binary(binary, operation_type, reduce_by=None):
    if isinstance(binary, types.FunctionType):
        binary, binary_file, files = py_wrapper.wrap(binary, operation_type, reduce_by)
        uploaded_files = _prepare_files([binary_file] + files)
        if config.REMOVE_TEMP_FILES:
            for file in files:
                os.remove(file)
        return binary, uploaded_files
    else:
        return binary, []

def _prepare_destination_tables(tables, replication_factor, compression_codec):
    if tables is None:
        if config.THROW_ON_EMPTY_DST_LIST:
            raise YtError("Destination tables are absent")
        return []
    tables = map(to_table, flatten(tables))
    for table in tables:
        if not exists(table.name):
            create_table(table.name, replication_factor=replication_factor, compression_codec=compression_codec)
        else:
            require(replication_factor is None,
                    YtError("Cannot append to table %s with set replication factor" %
                            table))
    return tables

def _remove_tables(tables):
    for table in tables:
        if exists(table) and get_type(table) == "table":
            remove(table)

def _add_user_command_spec(op_type, binary, input_format, output_format, files, file_paths, fds_count, memory_limit, reduce_by, spec):
    if binary is None:
        return spec, []
    files = _prepare_files(files)
    binary, additional_files = _prepare_binary(binary, op_type, reduce_by)
    binary = _add_output_fd_redirect(binary, fds_count)
    memory_limit = get_value(memory_limit, config.MEMORY_LIMIT)
    spec = update(
        {
            op_type: {
                "input_format": input_format.to_json(),
                "output_format": output_format.to_json(),
                "command": binary,
                "file_paths": flatten(files + additional_files + map(prepare_path, get_value(file_paths, []))),
                "memory_limit": memory_limit
            }
        },
    spec)
    return spec, files + additional_files

def _add_user_spec(spec):
    return update(
        {
            "mr_user": os.environ.get("MR_USER", ""),
            "system_user": os.environ.get("USER", ""),
            "wrapper_version": VERSION,
            "pool": os.environ.get("USER", "default")
        },
        spec)

def _add_input_output_spec(source_table, destination_table, spec):
    def get_input_name(table):
        return table.get_json()
    def get_output_name(table):
        return table.get_json()

    spec = update({"input_table_paths": map(get_input_name, source_table)}, spec)
    if isinstance(destination_table, TablePath):
        spec = update({"output_table_path": get_output_name(destination_table)}, spec)
    else:
        spec = update({"output_table_paths": map(get_output_name, destination_table)}, spec)
    return spec

def _add_table_writer_spec(job_types, table_writer, spec):
    if table_writer is not None:
        for job_type in flatten(job_types):
            spec = update({job_type: {"table_writer": table_writer}}, spec)
    return spec

def _make_operation_request(command_name, spec, strategy, finalizer=None, verbose=False):
    def run_operation(finalizer):
        operation = _make_transactional_request(command_name, {"spec": spec}, verbose=verbose)
        get_value(strategy, config.DEFAULT_STRATEGY).process_operation(command_name, operation, finalizer)

    if not config.DETACHED:
        transaction = PingableTransaction(config.OPERATION_TRANSACTION_TIMEOUT)
        def run_in_transaction():
            def envelope_finalizer():
                if finalizer is not None:
                    finalizer()
                transaction.__exit__(None, None, None)
            transaction.__enter__()
            run_operation(envelope_finalizer)
            transaction.__exit__(None, None, None)

        def finish_transaction():
            transaction.__exit__(None, None, None)

        execute_handling_sigint(run_in_transaction, finish_transaction)
    else:
        run_operation(finalizer)


class Buffer(object):
    """ Reads line iterator by chunks """
    def __init__(self, input_stream, format):
        self._input_stream = input_stream
        self._empty = False
        self._format = format

    def get(self, bytes=None):
        if bytes is None:
            bytes = config.WRITE_BUFFER_SIZE
        read_bytes = 0
        while read_bytes < bytes:
            row = self._format.read_row(self._input_stream)
            if not row:
                self._empty = True
                break
            read_bytes += len(row)
            yield row

    def empty(self):
        return self._empty

""" Common table methods """
def create_table(path, recursive=None, replication_factor=None, compression_codec=None, attributes=None):
    """ Creates empty table, use recursive for automatically creaation the path """
    table = TablePath(path)
    recursive = get_value(recursive, config.CREATE_RECURSIVE)
    attributes = get_value(attributes, {})
    if replication_factor is not None:
        attributes["replication_factor"] = replication_factor
    if compression_codec is not None:
        attributes["compression_codec"] = compression_codec
    create("table", table.name, recursive=recursive, attributes=attributes)

def create_temp_table(path=None, prefix=None):
    """ Creates temporary table by given path with given prefix """
    if path is None:
        path = config.TEMP_TABLES_STORAGE
        mkdir(path, recursive=True)
    else:
        path = to_name(path)
    require(exists(path), YtError("You cannot create table in unexisting path"))
    if prefix is not None:
        path = os.path.join(path, prefix)
    else:
        if not path.endswith("/"):
            path = path + "/"
    name = find_free_subpath(path)
    create_table(name)
    return name


def write_table(table, input_stream, format=None, table_writer=None, replication_factor=None, compression_codec=None):
    """
    Writes rows from input_stream to table.

    Input stream may be python file-like object or string, or list of strings. You also can
    use StringIterIO to wrap iter of strings.

    There are two modes.
    In chunk mode we write by portion of fixed size. Each portion is written with retries.
    In single mode we write all stream as is throw http.

    In both cases Transer-Encoding is used.
    """
    table = to_table(table)
    format = _prepare_format(format)
    if isinstance(input_stream, types.ListType):
        input_stream = StringIterIO(iter(input_stream))
    elif isinstance(input_stream, str):
        input_stream = StringIO(input_stream)

    with PingableTransaction(config.WRITE_TRANSACTION_TIMEOUT):
        if not exists(table.name):
            create_table(table.name, replication_factor=replication_factor, compression_codec=compression_codec)
        else:
            require(replication_factor is None,
                    YtError("Cannot write to existing table %s with set replication factor" % to_name(table)))

        if config.USE_RETRIES_DURING_WRITE and not isinstance(format, RawFormat):
            started = False
            buffer = Buffer(input_stream, format)
            while not buffer.empty():
                if started:
                    table.append = True
                params = {"path": table.get_json()}
                if table_writer is not None:
                    params["table_writer"] = table_writer
                for i in xrange(config.WRITE_RETRIES_COUNT):
                    try:
                        with PingableTransaction(config.WRITE_TRANSACTION_TIMEOUT):
                            _make_transactional_request(
                                "write",
                                params,
                                data=buffer.get(),
                                format=format,
                                proxy=get_host_for_heavy_operation())
                        break
                    except YtError as err:
                        print >>sys.stderr, "Retry", i + 1, "failed with message", str(err)
                        if i + 1 == config.WRITE_RETRIES_COUNT:
                            raise
                started = True
        else:
            params = {"path": table.get_json()}
            if table_writer is not None:
                params["table_writer"] = table_writer
            _make_transactional_request(
                "write",
                params,
                data=input_stream,
                format=format,
                proxy=get_host_for_heavy_operation())


def read_table(table, format=None, table_reader=None, response_type=None):
    """
    Downloads file from path.
    Response type means the output format. By default it is line generator.
    """
    table = to_table(table)
    format = _prepare_format(format)
    if config.TREAT_UNEXISTING_AS_EMPTY and not exists(table.name):
        return EMPTY_GENERATOR
    
    params = {"path": table.get_json()}
    if table_reader is not None:
        params["table_reader"] = table_reader

    response = _make_transactional_request(
        "read",
        params,
        format=format,
        raw_response=True)
    return read_content(response, get_value(response_type, "iter_lines"))

def _are_nodes(source_tables, destination_table):
    return len(source_tables) == 1 and not source_tables[0].has_delimiters() and not destination_table.append

def copy_table(source_table, destination_table, replace=True):
    """
    Copy table. Source table may be a list of tables. In this case tables would
    be merged.
    """
    if config.REPLACE_TABLES_WHILE_COPY_OR_MOVE: replace = True
    source_tables = _prepare_source_tables(source_table)
    if config.TREAT_UNEXISTING_AS_EMPTY and len(source_tables) == 0:
        return
    destination_table = to_table(destination_table)
    if _are_nodes(source_tables, destination_table):
        if replace and exists(destination_table.name) and to_name(source_tables[0]) != to_name(destination_table):
            # in copy destination should be absent
            remove(destination_table.name)
        mkdir(os.path.dirname(destination_table.name), recursive=True)
        copy(source_tables[0].name, destination_table.name)
    else:
        source_names = [table.name for table in source_tables]
        mode = "sorted" if all(map(is_sorted, source_names)) else "ordered"
        run_merge(source_tables, destination_table, mode)

def move_table(source_table, destination_table, replace=True):
    """
    Move table. Source table may be a list of tables. In this case tables would
    be merged.
    """
    if config.REPLACE_TABLES_WHILE_COPY_OR_MOVE: replace = True
    source_tables = _prepare_source_tables(source_table)
    if config.TREAT_UNEXISTING_AS_EMPTY and len(source_tables) == 0:
        return
    destination_table = to_table(destination_table)
    if _are_nodes(source_tables, destination_table):
        if source_tables[0] == destination_table:
            return
        if replace and exists(destination_table.name):
            remove(destination_table.name)
        move(source_tables[0].name, destination_table.name)
    else:
        copy_table(source_table, destination_table)
        for table in source_tables:
            if to_name(table) == to_name(destination_table):
                continue
            remove(table.name)

def run_erase(table, strategy=None):
    """
    Erase table. It differs from remove command.
    Erase only remove given content. You can erase range
    of records in the table.
    """
    table = to_table(table)
    if config.TREAT_UNEXISTING_AS_EMPTY and not exists(table.name):
        return
    _make_operation_request("erase", {"table_path": table.get_json()}, strategy)

def records_count(table):
    """Return number of records in the table"""
    table = to_name(table)
    if config.TREAT_UNEXISTING_AS_EMPTY and not exists(table):
        return 0
    return get_attribute(table, "row_count")

def get_size(table):
    """Return uncompressed size of the table"""
    table = to_name(table)
    if config.TREAT_UNEXISTING_AS_EMPTY and not exists(table):
        return 0
    return get_attribute(table, "uncompressed_data_size")

def is_empty(table):
    """Check table for the emptiness"""
    return records_count(to_name(table)) == 0

def get_sorted_by(table, default=None):
    if default is None:
        default = [] if config.TREAT_UNEXISTING_AS_EMPTY else None
    return get_attribute(to_name(table), "sorted_by", default=default)

def is_sorted(table):
    """Checks that table is sorted"""
    if config.USE_YAMR_SORT_REDUCE_COLUMNS:
        return get_sorted_by(table) == ["key", "subkey"]
    else:
        return parse_bool(get_attribute(to_name(table), "sorted", default="false"))

def run_merge(source_table, destination_table, mode=None,
              strategy=None, table_writer=None,
              replication_factor=None, compression_codec=None, spec=None):
    """
    Merge source tables and write it to destination table.
    Mode should be 'unordered', 'ordered', or 'sorted'.
    """
    source_table = _prepare_source_tables(source_table)
    destination_table = unlist(_prepare_destination_tables(destination_table, replication_factor, compression_codec))

    spec = compose(
        _add_user_spec,
        lambda _: _add_table_writer_spec("job_io", table_writer, _),
        lambda _: _add_input_output_spec(source_table, destination_table, _),
        lambda _: update({"mode": get_value(mode, "unordered")}, _),
        lambda _: get_value(_, {})
    )(spec)

    _make_operation_request("merge", spec, strategy, finalizer=None)

def run_sort(source_table, destination_table=None, sort_by=None,
             strategy=None, table_writer=None, replication_factor=None,
             compression_codec=None, spec=None):
    """
    Sort source table. If destination table is not specified, than it equals to source table.
    """

    sort_by = _prepare_sort_by(sort_by)
    source_table = _prepare_source_tables(source_table)
    for table in source_table:
        require(exists(table.name), YtError("Table %s should exist" % table))
    if all(is_prefix(sort_by, get_sorted_by(table.name, [])) for table in source_table):
        #(TODO) Hack detected: make something with it
        if len(source_table) > 0:
            run_merge(source_table, destination_table, "sorted",
                      strategy=strategy, table_writer=table_writer, spec=spec)
        return

    if destination_table is None:
        require(len(source_table) == 1 and not source_table[0].has_delimiters(),
                YtError("You must specify destination sort table "
                        "in case of multiple source tables"))
        destination_table = source_table[0]
    destination_table = unlist(_prepare_destination_tables(destination_table, replication_factor, compression_codec))

    if config.TREAT_UNEXISTING_AS_EMPTY and not source_table:
        _remove_tables([destination_table])
        return

    spec = compose(
        _add_user_spec,
        lambda _: _add_table_writer_spec(["sort_job_io", "merge_job_io"], table_writer, _),
        lambda _: _add_input_output_spec(source_table, destination_table, _),
        lambda _: update({"sort_by": sort_by}, _),
        lambda _: get_value(_, {})
    )(spec)

    _make_operation_request("sort", spec, strategy, finalizer=None)

""" Map and reduce methods """
class Finalizer(object):
    def __init__(self, files, output_tables):
        self.files = files if files is not None else []
        self.output_tables = output_tables

    def __call__(self):
        for table in map(to_name, self.output_tables):
            self.check_for_merge(table)
        if config.DELETE_EMPTY_TABLES:
            for table in map(to_name, self.output_tables):
                if is_empty(table):
                    remove_with_empty_dirs(table)
        for file in self.files:
            remove(file, force=True)

    def check_for_merge(self, table):
        chunk_count = int(get_attribute(table, "chunk_count"))
        if  chunk_count < config.MIN_CHUNK_COUNT_FOR_MERGE_WARNING:
            return

        # We use uncompressed data size to simplify recommended command
        chunk_size = float(get_attribute(table, "compressed_data_size")) / chunk_count
        if chunk_size > config.MAX_CHUNK_SIZE_FOR_MERGE_WARNING:
            return

        data_size_per_job = min(
                16 * 1024 * config.MB,
                int(500 * config.MB / float(get_attribute(table, "compression_ratio"))))

        mode = "sorted" if is_sorted(table) else "unordered"

        logger.warning("Chunks of output table {0} are too small. "
                       "This may cause suboptimal system performance. "
                       "If this table is not temporaty then consider running the following command:\n"
                       "mapreduce-yt -merge -mode {1} -src {0} -dst {0} "
                       "-ytspec '{{"
                          "\"combine_chunks\": \"true\", "
                          "\"job_io\":{{\"table_reader\":{{\"prefetch_window\":100}} }}, "
                          "\"data_size_per_job\": {2}"
                       "}}'".format(table, mode, data_size_per_job))

def run_map_reduce(mapper, reducer, source_table, destination_table,
                   format=None, input_format=None, output_format=None,
                   strategy=None, table_writer=None, spec=None,
                   replication_factor=None, compression_codec=None,
                   map_files=None, reduce_files=None,
                   map_file_paths=None, reduce_file_paths=None,
                   mapper_memory_limit=None, reducer_memory_limit=None,
                   sort_by=None, reduce_by=None):
    run_map_reduce.files_to_remove = []
    def memorize_files(spec, files):
        run_map_reduce.files_to_remove += files
        return spec

    source_table = _prepare_source_tables(source_table)
    destination_table = _prepare_destination_tables(destination_table, replication_factor, compression_codec)

    if config.TREAT_UNEXISTING_AS_EMPTY and not source_table:
        _remove_tables([destination_table])
        return

    input_format, output_format = _prepare_formats(format, input_format, output_format)

    spec = compose(
        _add_user_spec,
        lambda _: _add_table_writer_spec(["map_job_io", "reduce_job_io", "sort_job_io"], table_writer, _),
        lambda _: _add_input_output_spec(source_table, destination_table, _),
        lambda _: update({"sort_by": _prepare_sort_by(sort_by),
                          "reduce_by": _prepare_reduce_by(reduce_by)}, _),
        lambda _: memorize_files(*_add_user_command_spec("mapper", mapper, input_format, output_format, map_files, map_file_paths, 1, mapper_memory_limit, None, _)),
        lambda _: memorize_files(*_add_user_command_spec("reducer", reducer, input_format, output_format, reduce_files, reduce_file_paths, len(destination_table), reducer_memory_limit, reduce_by, _)),
        lambda _: get_value(_, {})
    )(spec)

    _make_operation_request("map_reduce", spec, strategy, Finalizer(run_map_reduce.files_to_remove, destination_table))

def run_operation(binary, source_table, destination_table,
                  files=None, file_paths=None,
                  format=None, input_format=None, output_format=None,
                  strategy=None,
                  table_writer=None,
                  replication_factor=None,
                  compression_codec=None,
                  job_count=None,
                  memory_limit=None,
                  spec=None,
                  op_name=None,
                  reduce_by=None):
    run_operation.files = []
    def memorize_files(spec, files):
        run_operation.files += files
        return spec

    source_table = _prepare_source_tables(source_table)
    if op_name == "reduce":
        if config.RUN_MAP_REDUCE_IF_SOURCE_IS_NOT_SORTED:
            are_input_tables_sorted =  all(
                is_prefix(_prepare_reduce_by(reduce_by), get_sorted_by(table.name, []))
                for table in source_table)
            if not are_input_tables_sorted:
                run_map_reduce(
                    mapper=None,
                    reducer=binary,
                    reduce_files=files,
                    reduce_file_paths=file_paths,
                    source_table=source_table,
                    destination_table=destination_table,
                    format=format,
                    input_format=input_format,
                    output_format=output_format,
                    table_writer=table_writer,
                    reduce_by=reduce_by,
                    sort_by=reduce_by,
                    spec=spec)
                return
            else:
                reduce_by = _prepare_reduce_by(reduce_by)
        else:
            reduce_by = _prepare_reduce_by(reduce_by)

    destination_table = _prepare_destination_tables(destination_table, replication_factor, compression_codec)
    input_format, output_format = _prepare_formats(format, input_format, output_format)

    if config.TREAT_UNEXISTING_AS_EMPTY and not source_table:
        _remove_tables(destination_table)
        return

    op_type = None
    if op_name == "map": op_type = "mapper"
    if op_name == "reduce": op_type = "reducer"

    spec = compose(
        _add_user_spec,
        lambda _: _add_table_writer_spec("job_io", table_writer, _),
        lambda _: _add_input_output_spec(source_table, destination_table, _),
        lambda _: update({"reduce_by": _prepare_reduce_by(reduce_by)}, _) if op_name == "reduce" else _,
        lambda _: update({"job_count": job_count}, _) if job_count is not None else _,
        lambda _: update({"memory_limit": memory_limit}, _) if memory_limit is not None else _,
        lambda _: memorize_files(*_add_user_command_spec(op_type, binary, input_format, output_format, files, file_paths, len(destination_table), memory_limit, reduce_by, _)),
        lambda _: get_value(_, {})
    )(spec)

    _make_operation_request(op_name, spec, strategy, Finalizer(run_operation.files, destination_table))

def run_map(binary, source_table, destination_table, **kwargs):
    kwargs["op_name"] = "map"
    run_operation(binary, source_table, destination_table, **kwargs)

def run_reduce(binary, source_table, destination_table, **kwargs):
    kwargs["op_name"] = "reduce"
    run_operation(binary, source_table, destination_table, **kwargs)


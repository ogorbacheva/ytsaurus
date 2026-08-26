## Python API

{% note info "Note" %}

Before you start, install the Python client from the pip repository using the command:

```bash
pip install ytsaurus-client
```

{% endnote %}

What becomes available after installing the package:

- The Python yt library.
- The CLI binary [yt]({{ docs_root }}/core/reference/api/cli/cli).
{{ core-user-guide-how-to-guides-api-python-start-md-audience-1 }}

Current package version requires **Python 3.8+**

### Installation { #install }

#### YSON libraries

To use the YSON format to work with tables, you need C++ bindings installed as a separate package. Installing [YSON bindings]({{ docs_root }}/core/how-to-guides/api/python/userdoc#yson_bindings):

```bash
pip install ytsaurus-yson
```

{% note warning "Attention!" %}

It is currently impossible to install YSON bindings on Windows.

{% endnote %}

{% note info "For Apple M1 platform users" %}

There are currently no YSON bindings built for the Apple platform. You can use [Rosetta 2](https://en.wikipedia.org/wiki/Rosetta_(software)) as a temporary solution and install the Python version for the x86_64 architecture.

Learn more [here](https://stackoverflow.com/questions/71691598/how-to-run-python-as-x86-with-rosetta2-on-arm-macos-machine).


{% endnote %}

To learn more about YSON, see [Formats]({{ docs_root }}/core/how-to-guides/api/python/userdoc#formats).

To find out the version of the installed Python wrapper, print the `yt.VERSION` variable or call the `yt --version` command.

If you encounter a problem, check the [FAQ](#faq) section. If the problem persists, write to the [chat](https://t.me/ytsaurus_ru).

[Library source code](https://github.com/ytsaurus/ytsaurus/tree/main/yt/python/yt/wrapper).

{% note warning "Attention!" %}

We do not recommend installing the library and its dependent packages in different ways at the same time. This can lead to problems that are difficult to diagnose.

{% endnote %}

#### Additional dependencies (extras) { #extras }

Some features require the additional dependencies that are not installed by default. They are declared in the extras sets of the `ytsaurus-client` package and are installed by specifying the set in square brackets:

| Set | Dependencies | Purpose |
| --- | --- | --- |
| `recommended` | `brotli`, `cryptography` | Recommended optional dependencies |
| `admin` | `kubernetes`, `docker` | [CLI admin commands]({{ docs_root }}/core/reference/cli-admin) (`yt admin logs k8s`, `yt admin metrics replay`) |

```bash
pip install "ytsaurus-client[recommended]"
pip install "ytsaurus-client[admin]"
```

### User documentation { #userdoc }
* [General]({{ docs_root }}/core/how-to-guides/api/python/userdoc#common)
   - [Agreements used in the code]({{ docs_root }}/core/how-to-guides/api/python/userdoc#agreements)
   - [Client]({{ docs_root }}/core/how-to-guides/api/python/userdoc#client)
      - [Thread safety]({{ docs_root }}/core/how-to-guides/api/python/userdoc#threadsafety)
      - [Asynchronous client based on gevent]({{ docs_root }}/core/how-to-guides/api/python/userdoc#gevent)
   - [Configuration]({{ docs_root }}/core/how-to-guides/api/python/userdoc#configuration)
      - [Shared config]({{ docs_root }}/core/how-to-guides/api/python/userdoc#configuration_common)
      - [Logging setup]({{ docs_root }}/core/how-to-guides/api/python/userdoc#configuration_logging)
      - [Token setup]({{ docs_root }}/core/how-to-guides/api/python/userdoc#configuration_token)
      - [Setting up retries]({{ docs_root }}/core/how-to-guides/api/python/userdoc#configuration_retries)
   - [Errors]({{ docs_root }}/core/how-to-guides/api/python/userdoc#errors)
   - [Formats]({{ docs_root }}/core/how-to-guides/api/python/userdoc#formats)
   - [YPath]({{ docs_root }}/core/how-to-guides/api/python/userdoc#ypath)
* [Teams]({{ docs_root }}/core/how-to-guides/api/python/userdoc#commands)
   - [Working with Cypress]({{ docs_root }}/core/how-to-guides/api/python/userdoc#cypress_commands)
   - [Working with files]({{ docs_root }}/core/how-to-guides/api/python/userdoc#file_commands)
   - [Working with tables]({{ docs_root }}/core/how-to-guides/api/python/userdoc#table_commands)
      - [Data classes]({{ docs_root }}/core/how-to-guides/api/python/userdoc#dataclass)
      - [Schemas] { ../../../api/python/userdoc.md#table_schema }
      - [TablePath]({{ docs_root }}/core/how-to-guides/api/python/userdoc#tablepath_class)
      - [Teams]({{ docs_root }}/core/how-to-guides/api/python/userdoc#table_commands)
      - [Parallel table reading]({{ docs_root }}/core/how-to-guides/api/python/userdoc#parallel_read)
      - [Parallel table writing]({{ docs_root }}/core/how-to-guides/api/python/userdoc#parallel_write)
   - [Working with transactions and locks]({{ docs_root }}/core/how-to-guides/api/python/userdoc#transaction_commands)
   - [Running operations]({{ docs_root }}/core/how-to-guides/api/python/userdoc#run_operation_commands)
      - [SpecBuilder]({{ docs_root }}/core/how-to-guides/api/python/userdoc#spec_builder)
   - [Working with operations and jobs]({{ docs_root }}/core/how-to-guides/api/python/userdoc#operation_and_job_commands)
      - [Operation]({{ docs_root }}/core/how-to-guides/api/python/userdoc#operation_class)
      - [OperationsTracker]({{ docs_root }}/core/how-to-guides/api/python/userdoc#operations_tracker_class)
      - [OperationsTrackerPool]({{ docs_root }}/core/how-to-guides/api/python/userdoc#operations_tracker_pool_class)
   - [Working with access permissions]({{ docs_root }}/core/how-to-guides/api/python/userdoc#acl_commands)
   - [Working with dynamic tables]({{ docs_root }}/core/how-to-guides/api/python/userdoc#dyntables_commands)
   - [Other commands]({{ docs_root }}/core/how-to-guides/api/python/userdoc#etc_commands)
* [Python objects as operations]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_operations)
   - [General information]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_operations_intro)
   - [Preparing an operation from a job]({{ docs_root }}/core/how-to-guides/api/python/userdoc#prepare_operation)
   - [Decorators]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_decorators)
   - [Pickling functions and environments]({{ docs_root }}/core/how-to-guides/api/python/userdoc#pickling)
      - [Running Python workloads in open-source environments]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_open_source_environment)
      - [Running the local script inside Docker with `respawn_in_docker`]({{ docs_root }}/core/how-to-guides/api/python/userdoc#respawn_in_docker)
      - [General structure]({{ docs_root }}/core/how-to-guides/api/python/userdoc#pickling_description)
      - [Link to a post with tips]({{ docs_root }}/core/how-to-guides/api/python/userdoc#pickling_advises)
   - [Porto layers]({{ docs_root }}/core/how-to-guides/api/python/userdoc#porto_layers)
   - [tmpfs in jobs]({{ docs_root }}/core/how-to-guides/api/python/userdoc#tmpfs_in_jobs)
   - [Statistics in jobs]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_jobs_statistics)
* [Untyped Python operations]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_operations_untyped)
   - [Decorators]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_decorators_untyped)
   - [Formats]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_formats)
      - [Structured data representation]({{ docs_root }}/core/how-to-guides/api/python/userdoc#structured_data)
      - [Control attributes]({{ docs_root }}/core/how-to-guides/api/python/userdoc#control_attributes)
      - [Other formats]({{ docs_root }}/core/how-to-guides/api/python/userdoc#other_formats)
* [Other]({{ docs_root }}/core/how-to-guides/api/python/userdoc#other)
   - [gRPC]({{ docs_root }}/core/how-to-guides/api/python/userdoc#grpc)
   - [YSON bindings]({{ docs_root }}/core/how-to-guides/api/python/userdoc#yson_bindings)
* [Deprecated]({{ docs_root }}/core/how-to-guides/api/python/userdoc#legacy)
   - [Python3 and byte strings]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python3_strings)

### Help { #pydoc }
The most up-to-date help on specific functions and their parameters is in the code.

To view a description of functions and classes in the interpreter, proceed as follows:

```bash
python
>>> import yt.wrapper as yt
>>> help(yt.run_sort)
```

### Examples { #examples }

* [Basic level]({{ docs_root }}/core/how-to-guides/api/python/examples#base)
   - [Reading and writing tables]({{ docs_root }}/core/how-to-guides/api/python/examples#read_write)
   - [Table schemas]({{ docs_root }}/core/how-to-guides/api/python/examples#table_schema)
   - [Simple map]({{ docs_root }}/core/how-to-guides/api/python/examples#simple_map)
   - [Sorting a table and a simple reduce operation]({{ docs_root }}/core/how-to-guides/api/python/examples#sort_and_reduce)
   - [Reduce with multiple input tables]({{ docs_root }}/core/how-to-guides/api/python/examples#reduce_multiple_output)
   - [Reduce with multiple input and output tables]({{ docs_root }}/core/how-to-guides/api/python/examples#reduce_multiple_input_output)
   - [mapreduce]({{ docs_root }}/core/how-to-guides/api/python/examples#map_reduce)
   - [MapReduce with multiple intermediate tables]({{ docs_root }}/core/how-to-guides/api/python/examples#map_reduce_multiple_intermediate_streams)
   - [Decorators for job classes]({{ docs_root }}/core/how-to-guides/api/python/examples#job_decorators)
   - [Working with files on the client and in operations]({{ docs_root }}/core/how-to-guides/api/python/examples#files)
   - [Grep]({{ docs_root }}/core/how-to-guides/api/python/examples#grep)
* [Advanced level]({{ docs_root }}/core/how-to-guides/api/python/examples#advanced)
   - [Batch queries]({{ docs_root }}/core/how-to-guides/api/python/examples#batch_queries)
   - [RPC]({{ docs_root }}/core/how-to-guides/api/python/examples#rpc)
* [Miscellaneous]({{ docs_root }}/core/how-to-guides/api/python/examples#misc)
   - [Data classes]({{ docs_root }}/core/how-to-guides/api/python/examples#dataclass)
   - [Context and managing writes to output tables]({{ docs_root }}/core/how-to-guides/api/python/examples#table_switches)
   - [Spec builders]({{ docs_root }}/core/how-to-guides/api/python/examples#spec_builder)
   - [Using gevent]({{ docs_root }}/core/how-to-guides/api/python/examples#gevent)
* [Untyped API]({{ docs_root }}/core/how-to-guides/api/python/examples#untyped_tutorial)
   - [Reading and writing tables]({{ docs_root }}/core/how-to-guides/api/python/examples#read_write_untyped)
   - [Simple map]({{ docs_root }}/core/how-to-guides/api/python/examples#simple_map_untyped)
   - [Sorting a table and a simple reduce operation]({{ docs_root }}/core/how-to-guides/api/python/examples#sort_and_reduce_untyped)
   - [Reduce with multiple input tables]({{ docs_root }}/core/how-to-guides/api/python/examples#reduce_multiple_output_untyped)
   - [Reduce with multiple input and output tables]({{ docs_root }}/core/how-to-guides/api/python/examples#reduce_multiple_input_output_untyped)
   - [MapReduce operation]({{ docs_root }}/core/how-to-guides/api/python/examples#map_reduce_untyped)
   - [Decorators for job classes and functions]({{ docs_root }}/core/how-to-guides/api/python/examples#job_decorators_untyped)
   - [Table switches and context]({{ docs_root }}/core/how-to-guides/api/python/examples#table_switches_untyped)
   - [Working with strings in Python3]({{ docs_root }}/core/how-to-guides/api/python/examples#yson_string_proxy)

<!-- ### Для разработчика { #fordeveloper }

  * [Контрибы](for_developer.md#contribs)
  * [Разбиение библиотеки на части в Аркадии](for_developer.md#peerdirs)
  * [Устройство и запуск тестов](for_developer.md#tests)
  * [Политика обновления библиотеки](for_developer.md#update_policy) -->

### FAQ { #faq }

This section contains answers to a number of frequently asked questions about the Python API. Answers to other frequently asked questions are in the [FAQ]({{ docs_root }}/core/reference/faq) section.

**Q: I installed the package via pypi, but I get the `yt: command not found` error.**
A: Try running the
`pip install ytsaurus-client --force-reinstall` command
, the log will most likely display a warning like `The script yt is installed in '...' which isn't on your PATH`. To solve the problem, you need to add the specified path to the PATH environment variable. To do this, run the following command:

```
echo 'export PATH="$PATH:<specified path>"' >> ~/.bashrc
source ~/.bashrc
```
Depending on the shell, the file may have a different name. The most common name on Mac is `~/.zshrc`.

**Q: Reading with retry ends with an error because of timeout.**
A: Most likely there are too many chunks in the table, you need to enlarge them. Use `yt merge --src table --dst table --spec "{combine_chunks=true}"`

**Q: The operation ends with a YSON error (for example: `YsonError: Premature end of stream`) and the web interface displays a YSON parsing error.**
A: The operation most likely writes to `stdout`. This is prohibited from being done explicitly in Python via `print, sys.stdout.write()` if the operation is not marked as `raw_io`, but it can be done by a third-party program, such as an archiver.

**Q: The Python library writes too much to stderr, how do I increase the level of logging?**
A: You can increase the level by setting the `YT_LOG_LEVEL="ERROR"` environment variable or by setting up the {{product-name}} logger: `logging.getLogger("Yt").setLevel(logging.ERROR)`.

**Q:  I start an operation on Mac OS X, but jobs end with errors like `ImportError: ./tmpfs/modules/_ctypes.so: invalid ELF header`.**
A: Since the Python wrapper takes all Python operation dependencies with it to the cluster, binary .so and .pyc files arrive there too, which then cannot be loaded. Use a porto layer with your local environment and enable filtering of these files so that they do not end up on the cluster. For more information, see the [section]({{ docs_root }}/core/how-to-guides/api/python/userdoc#porto_layers).

**Q: Jobs end with the `Invalid table index N: expected integer in range [A,B]` error.**
A: The message means that you output a table index in the records and there is no corresponding table. This most often means that you have several input tables and one output table. The `@table_index` fields appear in the input records by default. To disable them, you can change the format: `yt.config["tabular_data_format"] = yt.YsonFormat(process_table_index=None)`. To learn more about the format, see the [section]({{ docs_root }}/core/how-to-guides/api/python/userdoc#python_formats). As an alternative, explicitly indicate in the specification (example for a map operation): `{"mapper": {"enable_input_table_index": False}}`.

**Q: The (ReadTimeout, HTTPConnectionPool(....): Read timed out.) error appears after the operation is completed.**
The message means that the operation stderr could not be downloaded due to network problems and even repeated queries didn't help. In that case, you should use the `ignore_stderr_if_download_failed` option which enables you to ignore stderr if you can't download it. We recommend using this option when writing production processes.

**Q: I get the `Yson bindings required` error.**
This means that YSON was selected as the input (output) format and bindings could not be imported in the job. To learn more about YSON and bindings, see the [section]({{ docs_root }}/core/how-to-guides/api/python/userdoc#yson). You need to install the bindings package and check that YSON bindings are not filtered out using `module_filter`. This is a dynamic yson_lib.so library that can easily be accidentally filtered out when filtering out all .so files. In addition, so that `yt_yson_bindings` that came in modules are not deleted, write `config["pickling"]["ignore_yson_bindings_for_incompatible_platforms"] = False` in the configuration file.
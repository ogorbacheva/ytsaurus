from yt.common import update, get_value
import yt.logger as logger
import yt.wrapper as yt

def _get_compression_ratio(table, codec, yt_client, spec):
    logger.info("Compress sample of '%s' to calculate compression ratio", table) 
    tmp = yt_client.create_temp_table()
    spec["force_transform"] = "true"
    spec["title"] = "Merge to calculate compression ratio"
    yt_client.set(tmp + "/@compression_codec", codec)
    yt_client.run_merge(table + "[#1:#10000]", tmp, mode="unordered", spec=spec)
    ratio = yt_client.get(table + "/@compression_ratio")
    yt_client.remove(tmp)
    return ratio

def _check_codec(table, codec_name, codec_value, yt_client):
    if codec_value is None:
        return True
    else:
        try:
            codecs = yt_client.list("{0}/@{1}_statistics".format(table, codec_name))
            return codecs == [codec_value]
        except yt.YtResponseError as error:
            if error.is_resolve_error():
                return False
            else:
                raise

def convert_to_erasure(src, dst=None, erasure_codec=None, compression_codec=None, desired_chunk_size=None, yt_client=None, spec=None):
    spec = get_value(spec, {})

    if yt_client is None:
        yt_client = yt

    if erasure_codec is None or erasure_codec == "none":
        return

    if desired_chunk_size is None:
        desired_chunk_size = 2 * 1024 ** 3

    if not yt_client.exists(src) or yt_client.get(src + "/@row_count") == 0:
        logger.info("Table is empty")
        return False

    if dst is None:
        dst = src
    else:
        yt_client.create("table", dst, ignore_existing=True)
    
    if compression_codec is not None:
        ratio = _get_compression_ratio(src, compression_codec, yt_client, spec=spec)
        yt_client.set(dst + "/@compression_codec", compression_codec)
    else:
        ratio = yt_client.get(src + "/@compression_ratio")

    yt_client.set(dst + "/@erasure_codec", erasure_codec)
    if _check_codec(dst, "compression", compression_codec, yt_client) and _check_codec(dst, "erasure", erasure_codec, yt_client):
        logger.info("Table already has proper codecs")
        return False

    data_size_per_job = max(1, int(desired_chunk_size / ratio))
    mode = "sorted" if yt_client.is_sorted(src) else "unordered"
    
    spec = update(
        {
            "title": "Convert to erasure",
            "combine_chunks": "true",
            "force_transform": "true",
            "data_size_per_job": data_size_per_job,
            "job_io": {
                "table_writer": {
                    # TODO(ignat): added for copy_yamr_to_yt_pull. Add parameter.
                    "max_row_weight": 128 * 1024 * 1024,
                    "desired_chunk_size": desired_chunk_size
                }
            }
        },
        spec)
   
    logger.info("Merge from '%s' to '%s' (mode: '%s', spec: '%s'", src, dst, mode, spec) 
    yt_client.run_merge(src, dst, mode, spec=spec)


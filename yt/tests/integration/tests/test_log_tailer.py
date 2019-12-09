from yt_commands import *

import subprocess
import sys
import os.path

from yt_env_setup import YTEnvSetup

from distutils.spawn import find_executable

TEST_DIR = os.path.join(os.path.dirname(__file__))

YT_LOG_TAILER_BINARY = os.environ.get("YT_LOG_TAILER_BINARY")
if YT_LOG_TAILER_BINARY is None:
    YT_LOG_TAILER_BINARY = find_executable("ytserver-log-tailer")

YT_DUMMY_LOGGER_BINARY = os.environ.get("YT_DUMMY_LOGGER_BINARY")
if YT_DUMMY_LOGGER_BINARY is None:
    YT_DUMMY_LOGGER_BINARY = find_executable("dummy_logger")
#################################################################


class TestLogTailer(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    def _read_local_config_file(self, name):
        return open(os.path.join(TEST_DIR, "test_clickhouse", name)).read()

    def setup(self):
        if YT_LOG_TAILER_BINARY is None:
            pytest.skip("This test requires log_tailer binary being built")
        if YT_DUMMY_LOGGER_BINARY is None:
            pytest.skip("This test requires dummy_logger binary being built")

    @authors("gritukan")
    def test_log_rotation(self):
        log_tailer_config = yson.loads(open(os.path.join(TEST_DIR, "test_clickhouse", "log_tailer_config.yson")).read())
        log_path = \
            os.path.join(self.path_to_run,
            "logs",
            "dummy_logger",
            "log")

        log_tables = ["//sys/clickhouse/logs/log1", "//sys/clickhouse/logs/log2"]

        log_tailer_config["log_tailer"]["log_files"][0]["path"] = log_path
        log_tailer_config["log_tailer"]["log_files"][0]["table_paths"] = log_tables

        log_tailer_config["logging"]["writers"]["debug"]["file_name"] = \
            os.path.join(self.path_to_run,
            "logs",
            "dummy_logger",
            "log_tailer.debug.log")
        log_tailer_config["cluster_connection"] = self.__class__.Env.configs["driver"]

        os.mkdir(os.path.join(self.path_to_run, "logs", "dummy_logger"))
        log_tailer_config_file = \
            os.path.join(self.path_to_run,
            "logs",
            "dummy_logger",
            "log_tailer_config.yson")

        with open(log_tailer_config_file, "w") as config:
            config.write(yson.dumps(log_tailer_config, yson_format="pretty"))

        create_tablet_cell_bundle("sys")
        sync_create_cells(1, tablet_cell_bundle="sys")

        create("map_node", "//sys/clickhouse")
        create("map_node", "//sys/clickhouse/logs")

        create("table", "//sys/clickhouse/logs/log1", attributes= \
            {
                "dynamic": True,
                "schema": [
                    {"name": "timestamp", "type": "string", "sort_order": "ascending"},
                    {"name": "category", "type": "string"},
                    {"name": "message", "type": "string"},
                    {"name": "log_level", "type": "string"},
                    {"name": "thread_id", "type": "string"},
                    {"name": "fiber_id", "type": "string"},
                    {"name": "trace_id", "type": "string"},
                    {"name": "job_id", "type": "string"},
                    {"name": "operation_id", "type": "string"},
                ],
                "tablet_cell_bundle": "sys"
            })

        create("table", "//sys/clickhouse/logs/log2", attributes= \
            {
                "dynamic": True,
                "schema": [
                    {"name": "key_exression_column", "type": "uint64", "expression": "farm_hash(trace_id) % 123", "sort_order": "ascending"},
                    {"name": "trace_id", "type": "string", "sort_order": "ascending"},
                    {"name": "timestamp", "type": "string"},
                    {"name": "category", "type": "string"},
                    {"name": "message", "type": "string"},
                    {"name": "log_level", "type": "string"},
                    {"name": "thread_id", "type": "string"},
                    {"name": "fiber_id", "type": "string"},
                    {"name": "job_id", "type": "string"},
                    {"name": "operation_id", "type": "string"},
                ],
                "tablet_cell_bundle": "sys"
            })

        for log_table in log_tables:
            sync_mount_table(log_table)

        create_user("yt-log-tailer")
        add_member("yt-log-tailer", "superusers")

        dummy_logger = subprocess.Popen([YT_DUMMY_LOGGER_BINARY, log_path, "5", "1000"])
        log_tailer = subprocess.Popen([YT_LOG_TAILER_BINARY, str(dummy_logger.pid), "--config", log_tailer_config_file])

        time.sleep(6)
        log_tailer.terminate()
        dummy_logger.terminate()

        for log_table in log_tables:
            freeze_table(log_table)
            wait_for_tablet_state(log_table, "frozen")

            assert len(read_table(log_table)) == 1000
            remove(log_table)

from yt_env_setup import YTEnvSetup

import yt_commands

from yt_commands import (
    authors, create, get, read_journal, wait, read_table, write_journal,
    write_table, update_nodes_dynamic_config, get_singular_chunk_id, set_node_banned)

from yt_helpers import read_structured_log, write_log_barrier

import random
import time
import pytest

##################################################################


class TestNodeIOTrackingBase(YTEnvSetup):
    def setup(self):
        update_nodes_dynamic_config({
            "data_node": {
                "io_tracker": {
                    "enable": True,
                    "enable_raw": True,
                    "period_quant": 10,
                    "aggregation_period": 5000,
                }
            }
        })

    def get_structured_log_path(self, node_id=0):
        return "{}/logs/node-{}.json.log".format(self.path_to_run, node_id)

    def get_node_address(self, node_id=0):
        return "localhost:" + str(self.Env.configs["node"][node_id]["rpc_port"])

    def read_events(self, from_barrier=None, to_barrier=None, node_id=0, filter=None):
        # NB. We need to filter out the IO generated by internal cluster processes. For example, scheduler
        # sometimes writes to //sys/scheduler/event_log. Such events are also logged in the data node and
        # may lead to unexpected test failures.
        raw_events = read_structured_log(
            self.get_structured_log_path(node_id), from_barrier, to_barrier,
            category_filter={"IORaw"}, filter=filter)
        aggregate_events = read_structured_log(
            self.get_structured_log_path(node_id), from_barrier, to_barrier,
            category_filter={"IOAggregate"}, filter=filter)
        return raw_events, aggregate_events

    def wait_for_events(self, raw_count=None, aggregate_count=None, from_barrier=None, node_id=0, filter=None,
                        check_event_count=True):
        def is_ready():
            to_barrier = write_log_barrier(self.get_node_address(node_id), "Barrier")
            raw_events, aggregate_events = self.read_events(from_barrier, to_barrier, node_id, filter)
            return (raw_count is None or raw_count <= len(raw_events)) and \
                   (aggregate_count is None or aggregate_count <= len(aggregate_events))

        wait(is_ready)
        to_barrier = write_log_barrier(self.get_node_address(node_id=node_id), "Barrier")
        raw_events, aggregate_events = self.read_events(from_barrier, to_barrier, node_id, filter)
        if check_event_count:
            assert raw_count is None or raw_count == len(raw_events)
            assert aggregate_count is None or aggregate_count == len(aggregate_events)
        return raw_events, aggregate_events

    def generate_large_data(self, row_len=50000, row_count=5):
        rnd = random.Random(42)
        # NB. The values are chosen in such a way so they cannot be compressed or deduplicated.
        large_data = [{
            "id": i,
            "data": bytes(bytearray([rnd.randint(0, 255) for _ in range(row_len)]))
        } for i in range(row_count)]
        large_data_size = row_count * row_len
        return large_data, large_data_size

    def generate_large_journal(self, row_len=50000, row_count=5):
        rnd = random.Random(42)
        # NB. The values are chosen in such a way so they cannot be compressed or deduplicated.
        large_journal = [{"data": bytes(bytearray([rnd.randint(0, 255) for _ in range(row_len)]))} for _ in range(row_count)]
        large_journal_size = row_count * row_len
        return large_journal, large_journal_size

##################################################################


class TestDataNodeIOTracking(TestNodeIOTrackingBase):
    NUM_MASTERS = 1
    NUM_NODES = 1
    NUM_SCHEDULERS = 1

    DELTA_MASTER_CONFIG = {
        "cypress_manager": {
            "default_table_replication_factor": 1,
            "default_journal_read_quorum": 1,
            "default_journal_write_quorum": 1,
            "default_journal_replication_factor": 1,
        }
    }

    @authors("gepardo")
    def test_simple_write(self):
        from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
        create("table", "//tmp/table")
        write_table("//tmp/table", [{"a": 1, "b": 2, "c": 3}])
        raw_events, aggregate_events = self.wait_for_events(raw_count=1, aggregate_count=1, from_barrier=from_barrier)

        assert raw_events[0]["data_node_method@"] == "FinishChunk"
        for counter in ["byte_count", "io_count"]:
            assert raw_events[0][counter] > 0 and raw_events[0][counter] == aggregate_events[0][counter]

    @authors("gepardo")
    def test_two_chunks(self):
        from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
        create("table", "//tmp/table")
        write_table("//tmp/table", [{"number": 42, "good": True}])
        write_table("<append=%true>//tmp/table", [{"number": 43, "good": False}])
        raw_events, aggregate_events = self.wait_for_events(raw_count=2, aggregate_count=1, from_barrier=from_barrier)

        assert raw_events[0]["data_node_method@"] == "FinishChunk"
        assert raw_events[1]["data_node_method@"] == "FinishChunk"
        for counter in ["byte_count", "io_count"]:
            assert raw_events[0][counter] > 0
            assert raw_events[1][counter] > 0
            assert raw_events[0][counter] + raw_events[1][counter] == aggregate_events[0][counter]

    @authors("gepardo")
    def test_read_table(self):
        from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
        create("table", "//tmp/table")
        write_table("//tmp/table", [{"a": 1, "b": 2, "c": 3}])
        assert read_table("//tmp/table") == [{"a": 1, "b": 2, "c": 3}]
        raw_events, _ = self.wait_for_events(raw_count=2, from_barrier=from_barrier)

        assert raw_events[0]["data_node_method@"] == "FinishChunk"
        assert raw_events[1]["data_node_method@"] == "GetBlockSet"
        for counter in ["byte_count", "io_count"]:
            assert raw_events[0][counter] > 0
            assert raw_events[1][counter] > 0

    @authors("gepardo")
    def test_large_data(self):
        create("table", "//tmp/table")

        for i in range(10):
            large_data, large_data_size = self.generate_large_data()

            old_disk_space = get("//tmp/table/@resource_usage/disk_space")
            from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
            write_table("<append=%true>//tmp/table", large_data)
            _, aggregate_events = self.wait_for_events(aggregate_count=1, from_barrier=from_barrier, filter=lambda x: x["user@"] == "root")
            new_disk_space = get("//tmp/table/@resource_usage/disk_space")

            min_data_bound = 0.95 * large_data_size
            max_data_bound = 1.05 * (new_disk_space - old_disk_space)

            assert aggregate_events[0]["data_node_method@"] == "FinishChunk"
            assert min_data_bound <= aggregate_events[0]["byte_count"] <= max_data_bound
            assert aggregate_events[0]["io_count"] > 0

            from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
            assert read_table("//tmp/table[#{}:]".format(i * len(large_data))) == large_data
            _, aggregate_events = self.wait_for_events(aggregate_count=1, from_barrier=from_barrier, filter=lambda x: x["user@"] == "root")

            assert aggregate_events[0]["data_node_method@"] == "GetBlockSet"
            assert min_data_bound <= aggregate_events[0]["byte_count"] <= max_data_bound
            assert aggregate_events[0]["io_count"] > 0

    @authors("gepardo")
    def test_journal(self):
        data = [{"data":  str(i)} for i in range(20)]

        from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
        create("journal", "//tmp/journal")
        write_journal("//tmp/journal", data)
        raw_events, _ = self.wait_for_events(raw_count=1, from_barrier=from_barrier)

        assert raw_events[0]["data_node_method@"] == "FlushBlocks"
        assert raw_events[0]["byte_count"] > 0
        assert raw_events[0]["io_count"] > 0

        from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
        assert read_journal("//tmp/journal") == data
        raw_events, _ = self.wait_for_events(raw_count=1, from_barrier=from_barrier)

        assert raw_events[0]["data_node_method@"] == "GetBlockRange"
        assert raw_events[0]["byte_count"] > 0
        assert raw_events[0]["io_count"] > 0

    @authors("gepardo")
    def test_large_journal(self):
        create("journal", "//tmp/journal")

        for i in range(10):
            large_journal, large_journal_size = self.generate_large_journal(row_len=200000)
            min_data_bound = 0.9 * large_journal_size
            max_data_bound = 1.1 * large_journal_size

            from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
            write_journal("//tmp/journal", large_journal)
            raw_events, _ = self.wait_for_events(raw_count=1, from_barrier=from_barrier, filter=lambda x: x["user@"] == "root")

            assert raw_events[0]["data_node_method@"] == "FlushBlocks"
            assert min_data_bound <= raw_events[0]["byte_count"] <= max_data_bound
            assert raw_events[0]["io_count"] > 0

            from_barrier = write_log_barrier(self.get_node_address(), "Barrier")
            read_result = read_journal("//tmp/journal[#{}:#{}]".format(i * len(large_journal), (i + 1) * len(large_journal)))
            assert read_result == large_journal
            raw_events, _ = self.wait_for_events(raw_count=1, from_barrier=from_barrier, filter=lambda x: x["user@"] == "root")

            assert raw_events[0]["data_node_method@"] == "GetBlockRange"
            assert min_data_bound <= raw_events[0]["byte_count"] <= max_data_bound
            assert raw_events[0]["io_count"] > 0

##################################################################


class TestMasterJobsIOTracking(TestNodeIOTrackingBase):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    def _wait_for_merge(self, table_path, merge_mode, account="tmp"):
        yt_commands.set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        rows = read_table(table_path)
        assert get("{}/@resource_usage/chunk_count".format(table_path)) > 1

        yt_commands.set("{}/@chunk_merger_mode".format(table_path), merge_mode)
        yt_commands.set("//sys/accounts/{}/@merge_job_rate_limit".format(account), 10)
        yt_commands.set("//sys/accounts/{}/@chunk_merger_node_traversal_concurrency".format(account), 1)
        wait(lambda: get("{}/@resource_usage/chunk_count".format(table_path)) == 1)
        assert read_table(table_path) == rows

    @authors("gepardo")
    def test_replicate_chunk_writes(self):
        from_barriers = [write_log_barrier(self.get_node_address(node_id), "Barrier") for node_id in range(self.NUM_NODES)]
        create("table", "//tmp/table", attributes={"replication_factor": self.NUM_NODES})
        write_table("//tmp/table", [{"a": 1, "b": 2, "c": 3}])

        has_replication_job = False
        for node_id in range(self.NUM_NODES):
            raw_events, _ = self.wait_for_events(
                raw_count=1, from_barrier=from_barriers[node_id], node_id=node_id,
                filter=lambda event: event.get("data_node_method@") == "FinishChunk")
            if "job_type@" not in raw_events[0]:
                continue
            has_replication_job = True
            assert raw_events[0]["job_type@"] == "ReplicateChunk"
            assert "job_id" in raw_events[0]
            assert raw_events[0]["byte_count"] > 0
            assert raw_events[0]["io_count"] > 0

        assert has_replication_job

    @authors("gepardo")
    def test_large_replicate(self):
        large_data, large_data_size = self.generate_large_data()

        from_barriers = [write_log_barrier(self.get_node_address(node_id), "Barrier") for node_id in range(self.NUM_NODES)]
        create("table", "//tmp/table", attributes={"replication_factor": self.NUM_NODES})
        write_table("//tmp/table", large_data)

        disk_space = get("//tmp/table/@resource_usage/disk_space")
        min_data_bound = 0.95 * large_data_size
        max_data_bound = 1.05 * disk_space // self.NUM_NODES
        assert min_data_bound < max_data_bound

        events = []

        has_replication_write_job = False
        for node_id in range(self.NUM_NODES):
            raw_events, _ = self.wait_for_events(
                raw_count=1, from_barrier=from_barriers[node_id], node_id=node_id,
                filter=lambda event: event.get("data_node_method@") == "FinishChunk")
            event = raw_events[0]
            if "job_type@" not in event:
                continue
            has_replication_write_job = True
            events.append(event)
        assert has_replication_write_job

        time.sleep(3.0)

        has_replication_read_job = False
        for node_id in range(self.NUM_NODES):
            raw_events, _ = self.read_events(from_barrier=from_barriers[node_id], node_id=node_id)
            for event in raw_events:
                if "job_type@" in event and "data_node_method@" not in event:
                    has_replication_read_job = True
                    events.append(event)
                    break
        assert has_replication_read_job

        for event in events:
            assert event["job_type@"] == "ReplicateChunk"
            assert "job_id" in event
            assert min_data_bound <= event["byte_count"] <= max_data_bound

    @authors("gepardo")
    @pytest.mark.parametrize("merge_mode", ["deep", "shallow"])
    def test_merge_chunks(self, merge_mode):
        from_barrier = write_log_barrier(self.get_node_address(node_id=0), "Barrier")

        create("table", "//tmp/table")
        write_table("<append=true>//tmp/table", {"name": "cheetah", "type": "cat"})
        write_table("<append=true>//tmp/table", {"name": "fox", "type": "dog"})
        write_table("<append=true>//tmp/table", {"name": "wolf", "type": "dog"})
        write_table("<append=true>//tmp/table", {"name": "tiger", "type": "cat"})

        self._wait_for_merge("//tmp/table", merge_mode)

        _, aggregate_events = self.wait_for_events(
            aggregate_count=1, from_barrier=from_barrier, node_id=0,
            filter=lambda event: event.get("job_type@") == "MergeChunks" and event.get("data_node_method@") == "FinishChunk")
        assert aggregate_events[0]["byte_count"] > 0
        assert aggregate_events[0]["io_count"] > 0

    @authors("gepardo")
    @pytest.mark.parametrize("merge_mode", ["deep", "shallow"])
    def test_large_merge(self, merge_mode):
        large_data, large_data_size = self.generate_large_data()

        from_barrier = write_log_barrier(self.get_node_address(node_id=0), "Barrier")

        create("table", "//tmp/table")
        for row in large_data:
            write_table("<append=true>//tmp/table", row)

        disk_space = get("//tmp/table/@resource_usage/disk_space")
        min_data_bound = 0.95 * large_data_size
        max_data_bound = 1.05 * disk_space // self.NUM_NODES
        assert min_data_bound < max_data_bound

        self._wait_for_merge("//tmp/table", merge_mode)

        _, aggregate_events = self.wait_for_events(
            aggregate_count=1, from_barrier=from_barrier, node_id=0,
            filter=lambda event: event.get("job_type@") == "MergeChunks" and event.get("data_node_method@") == "FinishChunk")
        assert min_data_bound <= aggregate_events[0]["byte_count"] <= max_data_bound
        assert aggregate_events[0]["io_count"] > 0


class TestRepairMasterJobIOTracking(TestNodeIOTrackingBase):
    NUM_MASTERS = 1
    # We need six nodes to store chunks for reed_solomon_3_3 and one extra node to store repaired chunk.
    NUM_NODES = 7
    NUM_SCHEDULERS = 1

    @authors("gepardo")
    def test_repair_chunk(self):
        from_barriers = [write_log_barrier(self.get_node_address(node_id), "Barrier") for node_id in range(self.NUM_NODES)]

        create("table", "//tmp/table", attributes={"erasure_codec": "reed_solomon_3_3"})
        write_table("//tmp/table", [{"a": i, "b": 2 * i, "c": 3 * i} for i in range(100)])

        chunk_id = get_singular_chunk_id("//tmp/table")
        replicas = get("#{0}/@stored_replicas".format(chunk_id))
        address_to_ban = str(replicas[3])
        set_node_banned(address_to_ban, True)
        time.sleep(3.0)

        read_result = read_table("//tmp/table",
                                 table_reader={
                                     "unavailable_chunk_strategy": "restore",
                                     "pass_count": 1,
                                     "retry_count": 1,
                                 })
        assert read_result == [{"a": i, "b": 2 * i, "c": 3 * i} for i in range(100)]

        replicas = set(map(str, replicas))
        new_replicas = set(map(str, get("#{0}/@stored_replicas".format(chunk_id))))

        has_repaired_replica = True
        for node_id in range(self.NUM_NODES):
            address = self.get_node_address(node_id)
            if address in new_replicas and address not in replicas:
                has_repaired_replica = True
                raw_events, _ = self.wait_for_events(
                    raw_count=1, from_barrier=from_barriers[node_id], node_id=node_id,
                    filter=lambda event: "job_type@" in event)
                assert len(raw_events) == 1
                assert "job_id" in raw_events[0]
                assert raw_events[0]["byte_count"] > 0
                assert raw_events[0]["io_count"] > 0

        assert has_repaired_replica

        set_node_banned(address_to_ban, False)

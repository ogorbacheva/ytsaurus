from yt_env_setup import (
    YTEnvSetup)
from yt_commands import (
    authors, extract_statistic_v2, extract_deprecated_statistic, wait, create,
    ls, get,
    create_pool, read_table, write_table,
    map, run_test_vanilla, run_sleeping_vanilla,
    update_pool_tree_config, ban_node, unban_node)

from yt_helpers import read_structured_log, write_log_barrier

from yt.common import YtError

from collections import defaultdict
import time
import builtins


##################################################################


class TestEventLog(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 5
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 1
    USE_PORTO = True

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "event_log": {
                "flush_period": 1000,
            },
            "accumulated_usage_log_period": 1000,
            "accumulated_resource_usage_update_period": 100,
        }
    }

    DELTA_CONTROLLER_AGENT_CONFIG = {"controller_agent": {"event_log": {"flush_period": 1000}}}

    LOG_WRITE_WAIT_TIME = 0.5

    @authors("ignat")
    def test_scheduler_event_log(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"a": "b"}])
        op = map(
            in_="//tmp/t1",
            out="//tmp/t2",
            command='cat; bash -c "for (( I=0 ; I<=100*1000 ; I++ )) ; do echo $(( I+I*I )); done; sleep 2" >/dev/null && sleep 2',
        )

        def check_statistics(statistics, statistic_extractor):
            return statistic_extractor(statistics, "user_job.cpu.user") > 0 and \
                statistic_extractor(statistics, "user_job.max_memory") > 0 and \
                statistic_extractor(statistics, "user_job.block_io.bytes_read") is not None and \
                statistic_extractor(statistics, "user_job.current_memory.rss") is not None and \
                statistic_extractor(statistics, "user_job.cumulative_memory_mb_sec") > 0 and \
                statistic_extractor(statistics, "job_proxy.cpu.user") > 0

        wait(lambda: check_statistics(get(op.get_path() + "/@progress/job_statistics_v2"), extract_statistic_v2))
        wait(lambda: check_statistics(get(op.get_path() + "/@progress/job_statistics"), extract_deprecated_statistic))

        # wait for scheduler to dump the event log
        def check():
            def get_statistics(statistics, complex_key):
                result = statistics
                for part in complex_key.split("."):
                    if part:
                        if part not in result:
                            return None
                        result = result[part]
                return result

            res = read_table("//sys/scheduler/event_log")
            event_types = builtins.set()
            for item in res:
                event_types.add(item["event_type"])
                if item["event_type"] == "job_completed":
                    stats = item["statistics"]
                    user_time = get_statistics(stats, "user_job.cpu.user")
                    # our job should burn enough cpu
                    if user_time == 0:
                        return False
                if item["event_type"] == "job_started":
                    limits = item["resource_limits"]
                    if limits["cpu"] == 0:
                        return False
                    if limits["user_memory"] == 0:
                        return False
                    if limits["user_slots"] == 0:
                        return False
            if "operation_started" not in event_types:
                return False
            return True

        wait(check)

    @authors("ignat")
    def test_scheduler_event_log_buffering(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"a": "b"}])

        for node in ls("//sys/cluster_nodes"):
            ban_node(node, "test scheduler event log buffering")

        time.sleep(2)
        op = map(track=False, in_="//tmp/t1", out="//tmp/t2", command="cat")
        time.sleep(2)

        for node in ls("//sys/cluster_nodes"):
            unban_node(node)

        op.track()

        def check():
            try:
                res = read_table("//sys/scheduler/event_log")
            except YtError:
                return False
            event_types = builtins.set([item["event_type"] for item in res])
            for event in [
                "scheduler_started",
                "operation_started",
                "operation_completed",
            ]:
                if event not in event_types:
                    return False
            return True

        wait(check)

    @authors("ignat")
    def test_structured_event_log(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"a": "b"}])

        op = map(in_="//tmp/t1", out="//tmp/t2", command="cat")

        # Let's wait until scheduler dumps the information on our map operation
        def check_event_log():
            event_log = read_table("//sys/scheduler/event_log")
            for event in event_log:
                if event["event_type"] == "operation_completed" and event["operation_id"] == op.id:
                    return True
            return False

        wait(check_event_log)

        event_log = read_table("//sys/scheduler/event_log")

        scheduler_log_file = self.path_to_run + "/logs/scheduler-0.json.log"
        scheduler_address = ls("//sys/scheduler/instances")[0]
        scheduler_barrier = write_log_barrier(scheduler_address)

        controller_agent_log_file = self.path_to_run + "/logs/controller-agent-0.json.log"
        controller_agent_address = ls("//sys/controller_agents/instances")[0]
        controller_agent_barrier = write_log_barrier(controller_agent_address)

        structured_log = read_structured_log(scheduler_log_file, to_barrier=scheduler_barrier,
                                             row_filter=lambda e: "event_type" in e)
        structured_log += read_structured_log(controller_agent_log_file, to_barrier=controller_agent_barrier,
                                              row_filter=lambda e: "event_type" in e)

        for normal_event in event_log:
            flag = False
            for structured_event in structured_log:

                def key(event):
                    return (
                        event["timestamp"],
                        event["event_type"],
                        event["operation_id"] if "operation_id" in event else "",
                    )

                if key(normal_event) == key(structured_event):
                    flag = True
                    break
            assert flag

    @authors("eshcherbin")
    def test_split_fair_share_info_events(self):
        def check_keys(event, included_keys=None, excluded_keys=None):
            if included_keys is not None:
                for key in included_keys:
                    assert key in event
            if excluded_keys is not None:
                for key in excluded_keys:
                    assert key not in event

        def read_fair_share_info_events():
            event_log = read_table("//sys/scheduler/event_log", verbose=False)
            events_by_timestamp = defaultdict(list)
            events_by_snapshot_id = defaultdict(list)
            for event in event_log:
                if event["event_type"] == "fair_share_info":
                    check_keys(event, included_keys=["tree_id", "tree_snapshot_id"])
                    events_by_timestamp[event["timestamp"]].append(event)
                    events_by_snapshot_id[event["tree_snapshot_id"]].append(event)

            return events_by_timestamp, events_by_snapshot_id

        def read_latest_fair_share_info():
            events_by_timestamp, events_by_snapshot_id = read_fair_share_info_events()
            if not events_by_timestamp:
                return None

            for _, events in events_by_timestamp.items():
                assert len(frozenset(e["tree_snapshot_id"] for e in events)) == 1
            for _, events in events_by_snapshot_id.items():
                assert len(frozenset(e["timestamp"] for e in events)) == 1

            return events_by_timestamp[max(events_by_timestamp)]

        def check_events(expected_pools_batch_sizes, expected_operations_batch_sizes):
            events = read_latest_fair_share_info()
            if events is None:
                return False

            base_event_keys = ["pool_count", "resource_distribution_info"]
            pools_info_event_keys = ["pools", "pools_batch_index"]
            operations_info_event_keys = ["operations", "operations_batch_index"]

            base_event_count = 0
            actual_pool_batch_sizes = {}
            actual_operation_batch_sizes = {}
            for event in events:
                if "pools" in event:
                    check_keys(event, included_keys=pools_info_event_keys, excluded_keys=base_event_keys + operations_info_event_keys)
                    actual_pool_batch_sizes[event["pools_batch_index"]] = len(event["pools"])
                elif "operations" in event:
                    check_keys(event, included_keys=operations_info_event_keys, excluded_keys=base_event_keys + pools_info_event_keys)
                    actual_operation_batch_sizes[event["operations_batch_index"]] = len(event["operations"])
                else:
                    check_keys(event, included_keys=base_event_keys, excluded_keys=pools_info_event_keys + operations_info_event_keys)
                    base_event_count += 1

            assert base_event_count == 1

            assert sorted(actual_pool_batch_sizes) == list(range(len(actual_pool_batch_sizes)))
            actual_pool_batch_sizes = [actual_pool_batch_sizes[batch_index]
                                       for batch_index in range(len(actual_pool_batch_sizes))]

            assert sorted(actual_operation_batch_sizes) == list(range(len(actual_operation_batch_sizes)))
            actual_operation_batch_sizes = [actual_operation_batch_sizes[batch_index]
                                            for batch_index in range(len(actual_operation_batch_sizes))]

            return expected_pools_batch_sizes == actual_pool_batch_sizes and \
                expected_operations_batch_sizes == actual_operation_batch_sizes

        wait(lambda: check_events([1], []))

        update_pool_tree_config("default", {
            "max_event_log_pool_batch_size": 2,
            "max_event_log_operation_batch_size": 4,
        })

        for i in range(4):
            run_sleeping_vanilla()

        wait(lambda: check_events([2], [4]))

        update_pool_tree_config("default", {
            "max_event_log_pool_batch_size": 1,
            "max_event_log_operation_batch_size": 3,
        })

        wait(lambda: check_events([1, 1], [3, 1]))

    @authors("ignat")
    def test_accumulated_usage(self):
        create_pool("parent_pool", pool_tree="default")
        create_pool("test_pool", pool_tree="default", parent_name="parent_pool")

        scheduler_address = ls("//sys/scheduler/instances")[0]
        from_barrier = write_log_barrier(scheduler_address)

        op = run_test_vanilla("sleep 5.2", pool="test_pool", track=True)

        scheduler_log_file = self.path_to_run + "/logs/scheduler-0.json.log"

        time.sleep(self.LOG_WRITE_WAIT_TIME)

        to_barrier = write_log_barrier(scheduler_address)

        structured_log = read_structured_log(scheduler_log_file, from_barrier=from_barrier, to_barrier=to_barrier,
                                             row_filter=lambda e: "event_type" in e)

        found_accumulated_usage_event_with_op = False
        accumulated_usage = 0.0
        for event in structured_log:
            if event["event_type"] == "accumulated_usage_info":
                assert event["tree_id"] == "default"
                assert "pools" in event
                assert "test_pool" in event["pools"]
                assert "parent_pool" in event["pools"]
                assert event["pools"]["test_pool"]["parent"] == "parent_pool"
                assert event["pools"]["parent_pool"]["parent"] == "<Root>"

                assert "operations" in event
                if op.id in event["operations"]:
                    found_accumulated_usage_event_with_op = True
                    assert event["operations"][op.id]["pool"] == "test_pool"
                    assert event["operations"][op.id]["user"] == "root"
                    assert event["operations"][op.id]["operation_type"] == "vanilla"
                    accumulated_usage += event["operations"][op.id]["accumulated_resource_usage"]["cpu"]

            if event["event_type"] == "operation_completed":
                assert event["operation_id"] == op.id
                assert event["scheduling_info_per_tree"]["default"]["pool"] == "test_pool"
                assert event["scheduling_info_per_tree"]["default"]["ancestor_pools"] == ["parent_pool", "test_pool"]
                accumulated_usage += event["accumulated_resource_usage_per_tree"]["default"]["cpu"]

        assert accumulated_usage >= 5.0

        assert found_accumulated_usage_event_with_op

    @authors("ignat")
    def test_trimmed_annotations(self):
        scheduler_address = ls("//sys/scheduler/instances")[0]
        from_barrier = write_log_barrier(scheduler_address)

        op = run_test_vanilla(
            "sleep 1",
            pool="test_pool",
            spec={
                "annotations": {
                    "tag": "my_value",
                    "long_key": "x" * 200,
                    "nested_tag": {"key": "value"},
                }
            },
            track=True)

        time.sleep(1)

        scheduler_log_file = self.path_to_run + "/logs/scheduler-0.json.log"
        to_barrier = write_log_barrier(scheduler_address)

        structured_log = read_structured_log(scheduler_log_file, from_barrier=from_barrier, to_barrier=to_barrier,
                                             row_filter=lambda e: "event_type" in e)

        for event in structured_log:
            if event["event_type"] == "operation_completed":
                assert event["operation_id"] == op.id
                assert event["trimmed_annotations"]["tag"] == "my_value"
                assert len(event["trimmed_annotations"]["long_key"]) <= 150
                assert "nested_tag" not in event["trimmed_annotations"]


class TestEventLogWithoutAllocationService(TestEventLog):
    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "scheduler_connector": {
                "use_allocation_tracker_service": False,
            },
        },
    }

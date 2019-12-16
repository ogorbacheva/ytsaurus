from yt_env_setup import YTEnvSetup
from yt_commands import *

import yt.environment.init_operation_archive as init_operation_archive

from yt.test_helpers import wait
from yt.common import uuid_to_parts

import pytest

def _get_orchid_operation_path(op_id):
    return "//sys/scheduler/orchid/scheduler/operations/{0}/progress".format(op_id)

def _get_operation_from_cypress(op_id):
    result = get(get_operation_cypress_path(op_id) + "/@")
    if "full_spec" in result:
        result["full_spec"].attributes.pop("opaque", None)
    result["type"] = result["operation_type"]
    result["id"] = op_id
    return result

def _get_operation_from_archive(op_id):
    id_hi, id_lo = uuid_to_parts(op_id)
    archive_table_path = "//sys/operations_archive/ordered_by_id"
    rows = lookup_rows(archive_table_path, [{"id_hi": id_hi, "id_lo": id_lo}])
    if rows:
        return rows[0]
    else:
        return {}

def get_running_job_count(op_id):
    result = get_operation(op_id, attributes=["brief_progress"])
    return result["brief_progress"]["jobs"]["running"]

class TestGetOperation(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "watchers_update_period": 100,
            "operations_update_period": 10,
            "operations_cleaner": {
                "enable": False,
                "analysis_period": 100,
                # Cleanup all operations
                "hard_retained_operation_count": 0,
                "clean_delay": 0,
            },
            "static_orchid_cache_update_period": 100,
            "alerts_update_period": 100,
        },
    }

    def setup(self):
        sync_create_cells(1)
        init_operation_archive.create_tables_latest_version(self.Env.create_native_client(), override_tablet_cell_bundle="default")

    def teardown(self):
        remove("//sys/operations_archive")

    def clean_build_time(self, operation_result):
        del operation_result["brief_progress"]["build_time"]
        del operation_result["progress"]["build_time"]

    @authors("levysotsky", "babenko", "ignat")
    def test_get_operation(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        op = map(
            track=False,
            label="get_job_stderr",
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                }
            })
        wait_breakpoint()

        wait(lambda: exists(op.get_path()))

        wait(lambda: get_running_job_count(op.id) == 1)

        res_get_operation = get_operation(op.id, include_scheduler=True)
        res_cypress = _get_operation_from_cypress(op.id)
        res_orchid_progress = get(_get_orchid_operation_path(op.id))

        def filter_attrs(attrs):
            PROPER_ATTRS = [
                "id",
                "authenticated_user",
                "brief_spec",
                "runtime_parameters",
                "finish_time",
                "type",
                # COMPAT(levysotsky): Old name for "type"
                "operation_type",
                "result",
                "start_time",
                "state",
                "suspended",
                "spec",
                "unrecognized_spec",
                "full_spec",
                "slot_index_per_pool_tree",
                "alerts",
            ]
            return {key: attrs[key] for key in PROPER_ATTRS if key in attrs}

        assert filter_attrs(res_get_operation) == filter_attrs(res_cypress)

        res_get_operation_progress = res_get_operation["progress"]

        for key in res_orchid_progress:
            if key != "build_time":
                assert key in res_get_operation_progress

        release_breakpoint()
        op.track()

        res_cypress_finished = _get_operation_from_cypress(op.id)

        clean_operations()

        res_get_operation_archive = get_operation(op.id)

        for key in res_get_operation_archive.keys():
            if key in res_cypress:
                assert res_get_operation_archive[key] == res_cypress_finished[key]

    # Check that operation that has not been saved by operation cleaner
    # is reported correctly (i.e. "No such operation").
    # Actually, cleaner is disabled in this test,
    # but we emulate its work by removing operation node from Cypress.
    @authors("levysotsky")
    def test_get_operation_dropped_by_cleaner(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])
        op = map(
            track=False,
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
        )
        wait_breakpoint()

        wait(lambda: _get_operation_from_archive(op.id))

        release_breakpoint()
        op.track()

        remove(op.get_path(), force=True)
        with raises_yt_error(NoSuchOperation):
            get_operation(op.id)

    @authors("ilpauzner")
    def test_progress_merge(self):
        enable_operation_progress_archivation_path = "//sys/controller_agents/config/enable_operation_progress_archivation"
        set(enable_operation_progress_archivation_path, False, recursive=True)
        instances = ls("//sys/controller_agents/instances")
        assert len(instances) > 0
        wait(lambda: not get("//sys/controller_agents/instances/{}/orchid/controller_agent/config/enable_operation_progress_archivation".format(instances[0])))
        self.do_test_progress_merge()

    def do_test_progress_merge(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        op = map(
            track=False,
            label="get_job_stderr",
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                }
            })
        wait_breakpoint()

        id_hi, id_lo = uuid_to_parts(op.id)
        archive_table_path = "//sys/operations_archive/ordered_by_id"
        brief_progress = {"ivan": "ivanov", "build_time": "2100-01-01T00:00:00.000000Z"}
        progress = {"semen": "semenych", "semenych": "gorbunkov", "build_time": "2100-01-01T00:00:00.000000Z"}

        insert_rows(
            archive_table_path,
            [{"id_hi": id_hi, "id_lo": id_lo, "brief_progress": brief_progress, "progress": progress}],
            update=True)

        wait(lambda: _get_operation_from_archive(op.id))

        res_get_operation_new = get_operation(op.id)
        self.clean_build_time(res_get_operation_new)
        assert res_get_operation_new["brief_progress"] == {"ivan": "ivanov"}
        assert res_get_operation_new["progress"] == {"semen": "semenych", "semenych": "gorbunkov"}

        release_breakpoint()
        op.track()

        clean_operations()
        res_get_operation_new = get_operation(op.id)
        self.clean_build_time(res_get_operation_new)
        assert res_get_operation_new["brief_progress"] != {"ivan": "ivanov"}
        assert res_get_operation_new["progress"] != {"semen": "semenych", "semenych": "gorbunkov"}


    @authors("ignat")
    def test_attributes(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        op = map(
            track=False,
            label="get_job_stderr",
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                }
            })
        wait_breakpoint()

        assert list(get_operation(op.id, attributes=["state"])) == ["state"]
        assert list(get_operation(op.id, attributes=["progress"])) == ["progress"]
        with pytest.raises(YtError):
            get_operation(op.id, attributes=["PYSCH"])

        for read_from in ("cache", "follower"):
            res_get_operation = get_operation(op.id, attributes=["progress", "state"], include_scheduler=True, read_from=read_from)
            res_cypress = get(op.get_path() + "/@", attributes=["progress", "state"])

            assert sorted(list(res_get_operation)) == ["progress", "state"]
            assert "state" in sorted(list(res_cypress))
            assert res_get_operation["state"] == res_cypress["state"]
            assert ("alerts" in res_get_operation) == ("alerts" in res_cypress)
            if "alerts" in res_get_operation and "alerts" in res_cypress:
                assert res_get_operation["alerts"] == res_cypress["alerts"]

        with raises_yt_error(NoSuchAttribute):
            get_operation(op.id, attributes=["nonexistent-attribute-ZZZ"])

        release_breakpoint()
        op.track()

        clean_operations()

        assert list(get_operation(op.id, attributes=["progress"])) == ["progress"]

        requesting_attributes = ["progress", "runtime_parameters", "slot_index_per_pool_tree", "state"]
        res_get_operation_archive = get_operation(op.id, attributes=requesting_attributes)
        assert sorted(list(res_get_operation_archive)) == requesting_attributes
        assert res_get_operation_archive["state"] == "completed"
        assert res_get_operation_archive["runtime_parameters"]["scheduling_options_per_pool_tree"]["default"]["pool"] == "root"
        assert res_get_operation_archive["slot_index_per_pool_tree"]["default"] == 0

        with raises_yt_error(NoSuchAttribute):
            get_operation(op.id, attributes=["nonexistent-attribute-ZZZ"])

    @authors("asaitgalin", "levysotsky")
    def test_get_operation_and_half_deleted_operation_node(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        op = map(in_="//tmp/t1",
            out="//tmp/t2",
            command="cat")

        tx = start_transaction(timeout=300 * 1000)
        lock(op.get_path(),
            mode="shared",
            child_key="completion_transaction_id",
            transaction_id=tx)

        cleaner_path = "//sys/scheduler/config/operations_cleaner"
        set(cleaner_path + "/enable", True, recursive=True)
        wait(lambda: not exists("//sys/operations/" + op.id))
        wait(lambda: exists(op.get_path()))
        wait(lambda: "state" in get_operation(op.id))

    @authors("kiselyovp", "ilpauzner")
    def test_not_existing_operation(self):
        with raises_yt_error(NoSuchOperation):
            get_operation("00000000-00000000-0000000-00000001")

    @authors("ilpauzner")
    def test_archive_progress(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        op = map(
            track=False,
            label="get_job_stderr",
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                }
            })
        wait_breakpoint()

        wait(lambda: _get_operation_from_archive(op.id))

        row = _get_operation_from_archive(op.id)
        assert "progress" in row
        assert "brief_progress" in row

        release_breakpoint()
        op.track()

    @authors("ilpauzner")
    def test_archive_failure(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        # Unmount table to check that controller agent writes to Cypress during archive unavailability.
        sync_unmount_table("//sys/operations_archive/ordered_by_id")

        op = map(
            track=False,
            label="get_job_stderr",
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat ; BREAKPOINT"),
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                }
            })

        wait_breakpoint()
        wait(lambda: _get_operation_from_cypress(op.id).get("brief_progress", {}).get("jobs", {}).get("running") == 1)

        res_api = get_operation(op.id)
        self.clean_build_time(res_api)
        res_cypress = _get_operation_from_cypress(op.id)
        self.clean_build_time(res_cypress)

        assert res_api["brief_progress"] == res_cypress["brief_progress"]
        assert res_api["progress"] == res_cypress["progress"]

        sync_mount_table("//sys/operations_archive/ordered_by_id")

        wait(lambda: _get_operation_from_archive(op.id))
        assert _get_operation_from_archive(op.id).get("brief_progress", {}).get("jobs", {}).get("running") == 1
        release_breakpoint()
        op.track()

        res_api = get_operation(op.id)
        res_cypress = _get_operation_from_cypress(op.id)

        assert res_api["brief_progress"]["jobs"]["running"] == 0
        assert res_cypress["brief_progress"]["jobs"]["running"] > 0

        assert res_api["progress"]["jobs"]["running"] == 0
        assert res_cypress["progress"]["jobs"]["running"] > 0

        clean_operations()

        res_api = get_operation(op.id)
        res_archive = _get_operation_from_archive(op.id)

        assert res_api["brief_progress"] == res_archive["brief_progress"]
        assert res_api["progress"] == res_archive["progress"]

        # Unmount table again and check that error is _not_ "No such operation".
        sync_unmount_table("//sys/operations_archive/ordered_by_id")
        with raises_yt_error(TabletNotMounted):
            get_operation(op.id)

##################################################################

class TestGetOperationRpcProxy(TestGetOperation):
    USE_DYNAMIC_TABLES = True
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True
    ENABLE_HTTP_PROXY = True


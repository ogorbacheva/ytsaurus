from yt_env_setup import YTEnvSetup, Restarter, CONTROLLER_AGENTS_SERVICE, NODES_SERVICE

from yt_helpers import profiler_factory

from yt_commands import (
    authors, wait, retry, wait_assert, wait_breakpoint, release_breakpoint, with_breakpoint, create,
    update_controller_agent_config,
    create_pool, insert_rows,
    lookup_rows, delete_rows, write_table, map, vanilla, run_test_vanilla, abort_job, get_job, sync_create_cells, raises_yt_error)

import yt_error_codes

import yt.environment.init_operation_archive as init_operation_archive

from yt.common import date_string_to_datetime, uuid_to_parts, parts_to_uuid, update

from flaky import flaky

import pytest
import builtins
import datetime
from copy import deepcopy

JOB_ARCHIVE_TABLE = "//sys/operations_archive/jobs"
OPERATION_IDS_TABLE = "//sys/operations_archive/operation_ids"


def _delete_job_from_archive(op_id, job_id):
    op_id_hi, op_id_lo = uuid_to_parts(op_id)
    job_id_hi, job_id_lo = uuid_to_parts(job_id)
    delete_rows(
        JOB_ARCHIVE_TABLE,
        [
            {
                "operation_id_hi": op_id_hi,
                "operation_id_lo": op_id_lo,
                "job_id_hi": job_id_hi,
                "job_id_lo": job_id_lo,
            }
        ],
        atomicity="none",
    )


def _update_job_in_archive(op_id, job_id, attributes):
    op_id_hi, op_id_lo = uuid_to_parts(op_id)
    job_id_hi, job_id_lo = uuid_to_parts(job_id)
    attributes.update(
        {
            "operation_id_hi": op_id_hi,
            "operation_id_lo": op_id_lo,
            "job_id_hi": job_id_hi,
            "job_id_lo": job_id_lo,
        }
    )

    def do_update_job_in_archive():
        insert_rows(JOB_ARCHIVE_TABLE, [attributes], update=True, atomicity="none")
        return True

    wait(do_update_job_in_archive, ignore_exceptions=True)


def _get_job_from_archive(op_id, job_id):
    op_id_hi, op_id_lo = uuid_to_parts(op_id)
    job_id_hi, job_id_lo = uuid_to_parts(job_id)
    rows = lookup_rows(
        JOB_ARCHIVE_TABLE,
        [
            {
                "operation_id_hi": op_id_hi,
                "operation_id_lo": op_id_lo,
                "job_id_hi": job_id_hi,
                "job_id_lo": job_id_lo,
            }
        ],
    )
    return rows[0] if rows else None


def _get_controller_state_from_archive(op_id, job_id):
    wait(lambda: _get_job_from_archive(op_id, job_id) is not None)
    job_from_archive = _get_job_from_archive(op_id, job_id)
    return job_from_archive.get("controller_state")


class _TestGetJobBase(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True

    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "job_reporter": {
                "enabled": True,
                "reporting_period": 10,
                "min_repeat_delay": 10,
                "max_repeat_delay": 10,
            }
        },
    }

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "enable_job_reporter": True,
            "enable_job_spec_reporter": True,
            "enable_job_stderr_reporter": True,
        }
    }

    def setup_method(self, method):
        super(_TestGetJobBase, self).setup_method(method)
        sync_create_cells(1)
        init_operation_archive.create_tables_latest_version(
            self.Env.create_native_client(),
            override_tablet_cell_bundle="default",
        )

    def _check_get_job(
        self,
        op_id,
        job_id,
        before_start_time,
        state=None,
        has_spec=True,
        is_stale=False,
        archive_state=None,
        controller_state=None,
        pool=None,
        pool_tree=None,
    ):
        """None arguments mean do not check corresponding field in job"""

        job_info = retry(lambda: get_job(op_id, job_id))

        assert job_info["job_id"] == job_id
        assert job_info["operation_id"] == op_id
        assert job_info["type"] == "map"
        if state is not None:
            assert job_info["state"] == state
        if archive_state is not None:
            assert job_info["archive_state"] == archive_state
        if controller_state is not None:
            assert job_info["controller_state"] == controller_state
        if pool is not None:
            assert job_info["pool"] == pool
        if pool_tree is not None:
            assert job_info["pool_tree"] == pool_tree
        start_time = date_string_to_datetime(job_info["start_time"])
        assert before_start_time < start_time < datetime.datetime.utcnow()
        assert job_info.get("is_stale") == is_stale

        attributes = ["job_id", "state", "start_time"]
        job_info = retry(lambda: get_job(op_id, job_id, attributes=attributes))
        assert builtins.set(attributes).issubset(builtins.set(job_info.keys()))
        attribute_difference = builtins.set(job_info.keys()) - builtins.set(attributes)
        assert attribute_difference.issubset(builtins.set(["archive_state", "controller_state", "is_stale"]))
        assert job_info.get("is_stale") == is_stale

        def check_has_spec():
            job_info = retry(lambda: get_job(op_id, job_id))
            assert job_info.get("has_spec") == has_spec

        if has_spec is not None:
            wait_assert(check_has_spec)


class _TestGetJobCommon(_TestGetJobBase):
    @authors("levysotsky")
    def test_get_job(self):
        create_pool("my_pool")
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])
        before_start_time = datetime.datetime.utcnow()
        op = map(
            track=False,
            label="get_job",
            in_="//tmp/t1",
            out="//tmp/t2",
            spec={
                "scheduling_options_per_pool_tree": {
                    "default": {"pool": "my_pool"},
                },
                "mapper": {
                    "monitoring": {
                        "enable": True,
                        "sensor_names": ["cpu/user"],
                    },
                },
            },
            command=with_breakpoint(
                """
                echo SOME-STDERR >&2 ;
                cat ;
                if [[ "$YT_JOB_INDEX" == "0" ]]; then
                    BREAKPOINT
                    exit 1
                fi
            """
            ),
            fail_fast=False,
        )
        (job_id,) = wait_breakpoint()

        self._check_get_job(op.id, job_id, before_start_time, state="running", has_spec=None,
                            pool="my_pool", pool_tree="default")

        def correct_stderr_size():
            job_info = retry(lambda: get_job(op.id, job_id))
            return job_info.get("stderr_size", 0) == len("SOME-STDERR\n")

        wait(correct_stderr_size)

        release_breakpoint()
        op.track()

        self._check_get_job(op.id, job_id, before_start_time, state="failed", has_spec=True,
                            pool="my_pool", pool_tree="default")

        job_info = retry(lambda: get_job(op.id, job_id))
        assert job_info["fail_context_size"] > 0
        events = job_info["events"]
        assert len(events) > 0
        assert all(field in events[0] for field in ["phase", "state", "time"])

        _delete_job_from_archive(op.id, job_id)

        # Controller agent must be able to respond as it stores
        # zombie operation orchids.
        self._check_get_job(op.id, job_id, before_start_time, state="failed", has_spec=None)

    @authors("levysotsky")
    def test_operation_ids_table(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])
        op = map(
            track=False,
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("cat; BREAKPOINT"),
        )
        job_id, = wait_breakpoint()
        release_breakpoint()
        op.track()

        def get_operation_id_from_archive(job_id):
            job_id_hi, job_id_lo = uuid_to_parts(job_id)
            rows = lookup_rows(OPERATION_IDS_TABLE, [{
                "job_id_hi": job_id_hi,
                "job_id_lo": job_id_lo,
            }])
            if not rows:
                return None
            return parts_to_uuid(rows[0]["operation_id_hi"], rows[0]["operation_id_lo"])

        wait(lambda: get_operation_id_from_archive(job_id) is not None)
        assert get_operation_id_from_archive(job_id) == op.id


class TestGetJob(_TestGetJobCommon):
    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "operation_time_limit_check_period": 100,
            "snapshot_period": 500,
            "operations_update_period": 100,
        }
    }

    DELTA_NODE_CONFIG = update(_TestGetJobBase.DELTA_NODE_CONFIG, {
        "job_proxy_heartbeat_period":  100,
    })

    @authors("gritukan")
    def test_get_job_task_name_attribute_vanilla(self):
        op = vanilla(
            track=False,
            spec={
                "tasks": {
                    "master": {
                        "job_count": 1,
                        "command": with_breakpoint("BREAKPOINT", breakpoint_name="master"),
                    },
                    "slave": {
                        "job_count": 2,
                        "command": with_breakpoint("BREAKPOINT", breakpoint_name="slave"),
                    },
                },
            },
        )

        master_job_ids = wait_breakpoint(breakpoint_name="master", job_count=1)
        slave_job_ids = wait_breakpoint(breakpoint_name="slave", job_count=2)

        def check_task_names():
            for job_id in master_job_ids:
                job_info = retry(lambda: get_job(op.id, job_id))
                assert job_info["task_name"] == "master"
            for job_id in slave_job_ids:
                job_info = retry(lambda: get_job(op.id, job_id))
                assert job_info["task_name"] == "slave"

        check_task_names()

        release_breakpoint(breakpoint_name="master")
        release_breakpoint(breakpoint_name="slave")
        op.track()

        check_task_names()

    @authors("levysotsky")
    def test_get_stubborn_job(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])
        before_start_time = datetime.datetime.utcnow()
        op = map(
            track=False,
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("echo SOME-STDERR >&2; cat; BREAKPOINT"),
        )
        (job_id,) = wait_breakpoint()

        wait(lambda: _get_job_from_archive(op.id, job_id) is not None)
        job_from_archive = _get_job_from_archive(op.id, job_id)

        abort_job(job_id)
        release_breakpoint()
        op.track()

        # We emulate the situation when aborted (in CA's opinion) job
        # still reports "running" to archive.
        del job_from_archive["job_id_partition_hash"]
        del job_from_archive["operation_id_hash"]

        def _check_get_job():
            _update_job_in_archive(op.id, job_id, job_from_archive)
            job_info = retry(lambda: get_job(op.id, job_id))
            assert job_info["job_id"] == job_id
            assert job_info["archive_state"] == "running"
            controller_state = job_info.get("controller_state")
            if controller_state is None:
                assert job_info["is_stale"]
            else:
                assert controller_state == "aborted"
        wait_assert(_check_get_job)

        _delete_job_from_archive(op.id, job_id)

        self._check_get_job(
            op.id,
            job_id,
            before_start_time,
            state="aborted",
            controller_state="aborted",
            has_spec=None,
        )
        job_info = retry(lambda: get_job(op.id, job_id))
        assert "archive_state" not in job_info

    @authors("omgronny")
    @pytest.mark.parametrize("should_abort", [True, False])
    def test_get_controller_state_from_archive(self, should_abort):
        op = run_test_vanilla(with_breakpoint("BREAKPOINT"))

        (job_id,) = wait_breakpoint()

        wait(lambda: get_job(op.id, job_id).get("controller_state") == "running")

        wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "running")

        if should_abort:
            abort_job(job_id)
        release_breakpoint()
        op.track()

        if should_abort:
            wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "aborted")
        else:
            wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "completed")

    @authors("omgronny")
    def test_abort_unknown_jobs_in_archive(self):
        update_controller_agent_config("snapshot_period", 1000 * 1000)

        op = run_test_vanilla(with_breakpoint("BREAKPOINT"))

        (job_id,) = wait_breakpoint()

        wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "running")

        with Restarter(self.Env, CONTROLLER_AGENTS_SERVICE):
            pass

        wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "aborted")

    @authors("omgronny")
    def test_abort_vanished_jobs_in_archive(self):
        op = run_test_vanilla(with_breakpoint("BREAKPOINT"))

        (job_id,) = wait_breakpoint()

        wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "running")

        with Restarter(self.Env, NODES_SERVICE):
            pass

        release_breakpoint()
        op.track()

        _update_job_in_archive(op.id, job_id, {"transient_state": "running"})
        wait(lambda: _get_controller_state_from_archive(op.id, job_id) == "aborted")

    @authors("levysotsky")
    def test_not_found(self):
        with raises_yt_error(yt_error_codes.NoSuchOperation):
            get_job("1-2-3-4", "5-6-7-8")

        op = run_test_vanilla(
            with_breakpoint("BREAKPOINT"),
            job_count=1,
        )
        (job_id,) = wait_breakpoint()

        with raises_yt_error(yt_error_codes.NoSuchJob):
            get_job(op.id, "5-6-7-8")

        release_breakpoint()
        op.track()

    @authors("levysotsky")
    @flaky(max_runs=3)
    def test_get_job_is_stale_during_revival(self):
        op = run_test_vanilla(
            with_breakpoint("BREAKPOINT"),
            job_count=1,
            spec={"testing": {"delay_inside_revive": 5000}},
        )
        (job_id,) = wait_breakpoint()
        op.wait_for_fresh_snapshot()

        with Restarter(self.Env, CONTROLLER_AGENTS_SERVICE):
            pass

        with raises_yt_error(yt_error_codes.UncertainOperationControllerState):
            get_job(op.id, job_id)

        job_info = retry(lambda: get_job(op.id, job_id))
        assert job_info.get("controller_state") == "running"
        assert job_info.get("archive_state") == "running"
        assert not job_info.get("is_stale")

    @authors("levysotsky")
    def test_get_job_is_stale(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])
        op = map(
            track=False,
            in_="//tmp/t1",
            out="//tmp/t2",
            command=with_breakpoint("echo SOME-STDERR >&2; cat; BREAKPOINT"),
        )
        (job_id,) = wait_breakpoint()

        release_breakpoint()
        op.track()

        op.wait_for_state("completed")

        # We emulate the situation when completed job still reports "running" to archive.
        _update_job_in_archive(op.id, job_id, {"controller_state": "running", "transient_state": "running"})

        job_info = retry(lambda: get_job(op.id, job_id))
        assert job_info.get("controller_state") == "running"
        assert job_info.get("archive_state") == "running"
        assert job_info.get("is_stale")


class TestGetJobStatisticsLz4(_TestGetJobCommon):
    DELTA_NODE_CONFIG = deepcopy(_TestGetJobBase.DELTA_NODE_CONFIG)
    DELTA_NODE_CONFIG["exec_agent"]["job_reporter"]["report_statistics_lz4"] = True


class TestGetJobMonitoring(_TestGetJobBase):
    USE_PORTO = True

    @authors("levysotsky")
    def test_get_job_monitoring(self):
        op = run_test_vanilla(
            with_breakpoint("BREAKPOINT"),
            job_count=1,
            task_patch={
                "monitoring": {
                    "enable": True,
                    "sensor_names": ["cpu/user"],
                },
            },
        )
        job_id, = wait_breakpoint()

        wait(lambda: "monitoring_descriptor" in get_job(op.id, job_id))

        job = get_job(op.id, job_id)
        descriptor = job["monitoring_descriptor"]

        wait(
            lambda: profiler_factory().at_node(job["address"]).get("user_job/cpu/user", {"job_descriptor": descriptor})
            is not None)


##################################################################


class TestGetJobRpcProxy(TestGetJob):
    USE_DYNAMIC_TABLES = True
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True
    ENABLE_HTTP_PROXY = True


class TestGetJobStatisticsLz4RpcProxy(TestGetJobStatisticsLz4):
    USE_DYNAMIC_TABLES = True
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True
    ENABLE_HTTP_PROXY = True

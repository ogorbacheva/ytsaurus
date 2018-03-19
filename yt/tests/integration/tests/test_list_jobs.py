from yt_env_setup import wait, YTEnvSetup
from yt_commands import *
import yt.environment.init_operation_archive as init_operation_archive
from yt.wrapper.operation_commands import add_failed_operation_stderrs_to_error_message
from yt.common import date_string_to_datetime

from operations_archive import clean_operations

from time import sleep
from collections import defaultdict
from datetime import datetime

def validate_address_filter(op, include_archive, include_cypress, include_runtime):
    job_dict = defaultdict(list)
    res = list_jobs(op.id, include_archive=include_archive, include_cypress=include_cypress, include_runtime=include_runtime, data_source="manual")["jobs"]
    for job in res:
        address = job["address"]
        job_dict[address].append(job["id"])

    for address in job_dict.keys():
        res = list_jobs(op.id, include_archive=include_archive, include_cypress=include_cypress, include_runtime=include_runtime, data_source="manual", address=address)["jobs"]
        assert sorted([job["id"] for job in res]) == sorted(job_dict[address])

class TestListJobs(YTEnvSetup):
    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "statistics_reporter": {
                "enabled": True,
                "reporting_period": 10,
                "min_repeat_delay": 10,
                "max_repeat_delay": 10,
            }
        },
        "scheduler_connector": {
            "heartbeat_period": 100  # 100 msec
        },
        "job_proxy_heartbeat_period": 100,  # 100 msec

        # Turn off mount cache otherwise our statistic reporter would be unhappy
        # because of tablets of job statistics table are changed between tests.
        "cluster_connection": {
            "table_mount_cache": {
                "expire_after_successful_update_time": 0,
                "expire_after_failed_update_time": 0,
                "expire_after_access_time": 0,
                "refresh_time": 0,
            }
        },
    }

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "enable_job_reporter": True,
            "enable_job_spec_reporter": True,
            "static_orchid_cache_update_period": 100,
        },
    }

    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True

    def setup(self):
        self.sync_create_cells(1)
        init_operation_archive.create_tables_latest_version(self.Env.create_native_client())

    def teardown(self):
        remove("//sys/operations_archive")

    @add_failed_operation_stderrs_to_error_message
    def test_list_jobs(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")

        write_table("//tmp/t1", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        op = map_reduce(
            dont_track=True,
            wait_for_jobs=True,
            label="list_jobs",
            in_="//tmp/t1",
            out="//tmp/t2",
            precommand="echo STDERR-OUTPUT >&2; cat",
            mapper_command='test $YT_JOB_INDEX -eq "1" && exit 1',
            reducer_command="echo foo >&2; sleep 10; cat",
            sort_by="foo",
            reduce_by="foo",
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                },
                "map_job_count" : 3
            })

        jobs_path = "//sys/scheduler/orchid/scheduler/operations/{0}/running_jobs".format(op.id)

        progress_path = "//sys/scheduler/orchid/scheduler/operations/{0}/progress/jobs".format(op.id)
        wait(lambda: get(progress_path)["failed"] == 1)

        job_ids = op.jobs[:]

        validate_address_filter(op, False, False, True)

        aborted_jobs = []

        job_aborted = False
        for job in job_ids:
            if job in get(jobs_path):
                abort_job(job)
                aborted_jobs.append(job)
                job_aborted = True
                break
        assert job_aborted

        sleep(1)

        for job in job_ids:
            op.resume_job(job)
        op.ensure_jobs_running()

        res = list_jobs(op.id, data_source="manual", include_cypress=True, include_scheduler=False, include_archive=False, job_state="completed")["jobs"]
        assert len(res) == 0

        res = list_jobs(op.id, data_source="manual", include_cypress=False, include_scheduler=True, include_archive=False)["jobs"]
        assert len(res) == 3
        assert all(job["state"] == "running" for job in res)
        assert all(job["type"] == "partition_map" for job in res)

        op.resume_jobs()
        op.track()

        jobs = get("//sys/operations/{}/jobs".format(op.id), attributes=[
            "job_type",
            "state",
            "start_time",
            "finish_time",
            "address",
            "error",
            "statistics",
            "size",
            "uncompressed_data_size"
        ])

        completed_jobs = []
        map_jobs = []
        map_failed_jobs = []
        reduce_jobs = []
        jobs_with_stderr = []
        jobs_without_stderr = []

        for job_id, job in jobs.iteritems():
            if job.attributes["job_type"] == "partition_map":
                map_jobs.append(job_id)
                if job.attributes["state"] == "failed":
                    map_failed_jobs.append(job_id)
            if job.attributes["job_type"] == "partition_reduce":
                reduce_jobs.append(job_id)
            if job.attributes["state"] == "completed":
                completed_jobs.append(job_id)
            if "stderr" in job:
                jobs_with_stderr.append(job_id)
            else:
                jobs_without_stderr.append(job_id)

        manual_options = dict(data_source="manual", include_cypress=True, include_scheduler=True, include_archive=False)
        runtime_options = dict(data_source="runtime")
        auto_options = dict(data_source="auto")
        for options in (manual_options, runtime_options, auto_options):
            res = list_jobs(op.id, **options)
            for key in res["type_counts"]:
                correct = 0
                if key == "partition_reduce":
                    correct = 1
                if key == "partition_map":
                    correct = 5
                assert res["type_counts"][key] == correct
            for key in res["state_counts"]:
                correct = 0
                if key == "completed":
                    correct = 4
                if key == "failed" or key == "aborted":
                    correct = 1
                assert res["state_counts"][key] == correct
            assert res["cypress_job_count"] == 6
            assert res["scheduler_job_count"] == 0
            assert res["archive_job_count"] == yson.YsonEntity()

            res = list_jobs(op.id, type="partition_reduce", **options)
            for key in res["type_counts"]:
                correct = 0
                if key == "partition_reduce":
                    correct = 1
                if key == "partition_map":
                    correct = 5
                assert res["type_counts"][key] == correct
            for key in res["state_counts"]:
                correct = 0
                if key == "completed":
                    correct = 1
                assert res["state_counts"][key] == correct
            assert res["cypress_job_count"] == 6
            assert res["scheduler_job_count"] == 0
            assert res["archive_job_count"] == yson.YsonEntity()

            res = list_jobs(op.id, job_state="failed", **options)["jobs"]
            assert sorted(map_failed_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_type="partition_map", **options)["jobs"]
            assert sorted(map_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_type="partition_reduce", **options)["jobs"]
            assert sorted(reduce_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_state="completed", **options)["jobs"]
            assert sorted(completed_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id,  with_stderr=True, **options)["jobs"]
            assert sorted(jobs_with_stderr) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, with_stderr=False, **options)["jobs"]
            assert sorted(jobs_without_stderr) == sorted([job["id"] for job in res])

            validate_address_filter(op, False, True, False)

        clean_operations(self.Env.create_native_client())
        sleep(1)  # statistics_reporter

        manual_options = dict(data_source="manual", include_cypress=False, include_scheduler=False, include_archive=True)
        archive_options = dict(data_source="archive")
        auto_options = dict(data_source="auto")

        for options in (manual_options, archive_options, auto_options):
            res = list_jobs(op.id, **options)
            assert res["cypress_job_count"] == yson.YsonEntity()
            assert res["scheduler_job_count"] == yson.YsonEntity()
            assert res["archive_job_count"] == 6

            res = res["jobs"]
            assert sorted(jobs.keys()) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, offset=4, limit=3, sort_field="start_time", **options)["jobs"]
            assert len(res) == 2
            assert res == sorted(res, key=lambda item: item["start_time"])

            res = list_jobs(op.id, offset=0, limit=2, sort_field="start_time", sort_order="descending", **options)["jobs"]
            assert len(res) == 2
            assert res == sorted(res, key=lambda item: item["start_time"], reverse=True)

            res = list_jobs(op.id, offset=0, limit=2, sort_field="id", **options)["jobs"]
            assert len(res) == 2
            assert res == sorted(res, key=lambda item: item["id"])

            res = list_jobs(op.id, job_state="completed", **options)["jobs"]
            assert sorted(completed_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_state="aborted", **options)["jobs"]
            assert sorted(aborted_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_type="partition_map", **options)["jobs"]
            assert sorted(map_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_type="partition_reduce", **options)["jobs"]
            assert sorted(reduce_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, job_state="failed", **options)["jobs"]
            assert sorted(map_failed_jobs) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, with_stderr=True, **options)["jobs"]
            assert sorted(jobs_with_stderr) == sorted([job["id"] for job in res])

            res = list_jobs(op.id, with_stderr=False, **options)["jobs"]
            assert sorted(jobs_without_stderr) == sorted([job["id"] for job in res])

            validate_address_filter(op, True, False, False)

    def test_running_jobs_stderr_size(self):
        create("table", "//tmp/input")
        create("table", "//tmp/output")

        write_table("//tmp/input", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        events = EventsOnFs()
        op = map(
            dont_track=True,
            wait_for_jobs=True,
            in_="//tmp/input",
            out="//tmp/output",
            command="echo MAPPER-STDERR-OUTPUT >&2 ; cat ; {wait_cmd}".format(
                wait_cmd=events.wait_event_cmd("can_finish_mapper")))
        op.resume_jobs()

        def get_stderr_size():
            return get("//sys/scheduler/orchid/scheduler/operations/{0}/running_jobs/{1}/stderr_size".format(op.id, op.jobs[0]))
        wait(lambda: get_stderr_size() == len("MAPPER-STDERR-OUTPUT\n"))

        options = dict(data_source="manual", include_cypress=False, include_scheduler=True, include_archive=False)

        res = list_jobs(op.id, **options)
        assert sorted(job["id"] for job in res["jobs"]) == sorted(op.jobs)
        for job in res["jobs"]:
            assert job["stderr_size"] == len("MAPPER-STDERR-OUTPUT\n")

        res = list_jobs(op.id, with_stderr=True, **options)
        for job in res["jobs"]:
            assert job["stderr_size"] == len("MAPPER-STDERR-OUTPUT\n")
        assert sorted(job["id"] for job in res["jobs"]) == sorted(op.jobs)

        res = list_jobs(op.id, with_stderr=False, **options)
        assert res["jobs"] == []

        events.notify_event("can_finish_mapper")
        op.track()

    def test_aborted_jobs(self):
        create("table", "//tmp/input")
        create("table", "//tmp/output")

        write_table("//tmp/input", [{"foo": "bar"}, {"foo": "baz"}, {"foo": "qux"}])

        now = datetime.utcnow()

        events = EventsOnFs()
        op = map(
            dont_track=True,
            wait_for_jobs=True,
            in_="//tmp/input",
            out="//tmp/output",
            command="cat ; {wait_cmd}".format(wait_cmd=events.wait_event_cmd("can_finish_mapper")),
            spec={"job_count": 3})

        jobs_path = "//sys/scheduler/orchid/scheduler/operations/{0}/running_jobs".format(op.id)
        jobs = get(jobs_path)
        assert jobs
        abort_job(jobs.keys()[0])

        op.resume_jobs()

        events.notify_event("can_finish_mapper")
        op.track()

        options = dict(data_source="manual", include_cypress=True, include_scheduler=True, include_archive=True)

        res = list_jobs(op.id, **options)["jobs"]
        assert any(job["state"] == "aborted" for job in res)
        assert all((date_string_to_datetime(job["start_time"]) > now) for job in res)
        assert all((date_string_to_datetime(job["finish_time"]) >= date_string_to_datetime(job["start_time"])) for job in res)

    def test_running_aborted_jobs(self):
        create("table", "//tmp/input")
        create("table", "//tmp/output")

        write_table("//tmp/input", [{"foo": "bar"}])

        op = map(
            dont_track=True,
            in_="//tmp/input",
            out="//tmp/output",
            command='if [ "$YT_JOB_INDEX" = "0" ]; then sleep 1000; fi;')

        jobs_path = "//sys/scheduler/orchid/scheduler/operations/{0}/running_jobs".format(op.id)
        wait(lambda: get(jobs_path))
        wait(lambda: len(list_jobs(op.id, include_archive=True, include_cypress=False, include_scheduler=False, data_source="manual")["jobs"]) == 1)

        unmount_table("//sys/operations_archive/jobs")
        wait(lambda: get("//sys/operations_archive/jobs/@tablet_state") == "unmounted")

        self.Env.kill_nodes()
        self.Env.start_nodes()

        clear_metadata_caches()

        self.wait_for_cells(ls("//sys/tablet_cells"))

        mount_table("//sys/operations_archive/jobs")
        wait(lambda: get("//sys/operations_archive/jobs/@tablet_state") == "mounted")

        op.track()

        get("//sys/operations/" + op.id + "/jobs")

        time.sleep(1)

        options = dict(data_source="manual", include_cypress=False, include_scheduler=False, include_archive=True)
        select_rows("* from [//sys/operations_archive/jobs]")
        jobs = list_jobs(op.id, running_jobs_lookbehind_period=1000, **options)["jobs"]
        assert len(jobs) == 1


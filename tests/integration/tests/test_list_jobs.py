from yt_env_setup import wait, YTEnvSetup
from yt_commands import *
import yt.environment.init_operation_archive as init_operation_archive
from yt.wrapper.common import uuid_hash_pair
from yt.common import date_string_to_timestamp_mcs
from yt.wrapper.operation_commands import add_failed_operation_stderrs_to_error_message

import __builtin__
import datetime
import itertools
import pytest
import shutil

from time import sleep
from collections import defaultdict

def id_to_parts(id):
    id_parts = id.split("-")
    id_hi = long(id_parts[2], 16) << 32 | int(id_parts[3], 16)
    id_lo = long(id_parts[0], 16) << 32 | int(id_parts[1], 16)
    return id_hi, id_lo

def validate_address_filter(op, include_archive, include_cypress, include_runtime):
    job_dict = defaultdict(list)
    res = list_jobs(op.id, include_archive=include_archive, include_cypress=include_cypress, include_runtime=include_runtime)["jobs"]
    for job in res:
        address = job["address"]
        job_dict[address].append(job["id"])

    for address in job_dict.keys():
        res = list_jobs(op.id, include_archive=include_archive, include_cypress=include_cypress, include_runtime=include_runtime, address=address)["jobs"]
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
            precommand="echo STDERR-OUTPUT >&2",
            mapper_command='test $YT_JOB_INDEX -eq "1" && exit 1',
            reducer_command="cat",
            sort_by="foo",
            reduce_by="foo",
            spec={
                "mapper": {
                    "input_format": "json",
                    "output_format": "json"
                },
                "job_count" : 3
            })

        job_ids = op.jobs[:]

        res = list_jobs(op.id, include_archive=False, include_runtime=True)["jobs"]
        assert sorted(job_ids) == sorted([job["id"] for job in res])
 
        res = list_jobs(op.id, include_archive=False, include_runtime=True, has_stderr=False)["jobs"]
        assert len(res) == 0

        res = list_jobs(op.id, include_archive=False, include_runtime=True, has_stderr=True)["jobs"]
        assert sorted(job_ids) == sorted([job["id"] for job in res])

        validate_address_filter(op, False, False, True)

        aborted_jobs = []

        for job in job_ids:
            abort_job(job)
            aborted_jobs.append(job)

        sleep(1)
       
        for job in job_ids:
            op.resume_job(job)
        op.ensure_jobs_running()

        res = list_jobs(op.id, include_archive=False, include_cypress=True, job_state="completed")["jobs"]
        assert len(res) == 0 

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

        res = list_jobs(op.id, include_archive=False, include_cypress=True, job_state="failed")["jobs"]
        assert sorted(map_failed_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, include_archive=False, include_cypress=True, job_type="partition_map")["jobs"]
        assert sorted(map_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, include_archive=False, include_cypress=True, job_type="partition_reduce")["jobs"]
        assert sorted(reduce_jobs) == sorted([job["id"] for job in res]) 

        res = list_jobs(op.id, include_archive=False, include_cypress=True, job_state="completed")["jobs"]
        assert sorted(completed_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, include_archive=False, include_cypress=True, has_stderr=True)["jobs"]
        assert sorted(jobs_with_stderr) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, include_archive=False, include_cypress=True, has_stderr=False)["jobs"]
        assert sorted(jobs_without_stderr) == sorted([job["id"] for job in res]) 

        validate_address_filter(op, False, True, False)

        jobs_archive_path = "//sys/operations_archive/jobs"

        rows = []

        for job_id, job in jobs.iteritems():
            op_id_hi, op_id_lo = id_to_parts(op.id)
            id_hi, id_lo = id_to_parts(job_id)
            row = {}
            row["operation_id_hi"] = yson.YsonUint64(op_id_hi)
            row["operation_id_lo"] = yson.YsonUint64(op_id_lo)
            row["job_id_hi"] = yson.YsonUint64(id_hi)
            row["job_id_lo"] = yson.YsonUint64(id_lo)
            row["type"] = job.attributes["job_type"]
            row["state"] = job.attributes["state"]
            row["start_time"] = date_string_to_timestamp_mcs(job.attributes["start_time"])
            row["finish_time"] = date_string_to_timestamp_mcs(job.attributes["finish_time"])
            row["address"] = job.attributes["address"]
            if "stderr" in job:
                row["stderr_size"] = job["stderr"].attributes["uncompressed_data_size"]
            rows.append(row)

        insert_rows(jobs_archive_path, rows)

        remove("//sys/operations/{}".format(op.id))

        sleep(1)  # statistics_reporter
        res = list_jobs(op.id)["jobs"]
        assert sorted(jobs.keys()) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, offset=1, limit=3, sort_field="start_time")["jobs"]
        assert len(res) == 2
        assert res == sorted(res, key=lambda item: item["start_time"])

        res = list_jobs(op.id, offset=0, limit=2, sort_field="start_time", sort_order="descending")["jobs"]
        assert len(res) == 2
        assert res == sorted(res, key=lambda item: item["start_time"], reverse=True)

        res = list_jobs(op.id, job_state="completed")["jobs"]
        assert sorted(completed_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, job_state="aborted")["jobs"]
        assert sorted(aborted_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, job_type="partition_map")["jobs"]
        assert sorted(map_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, job_type="partition_reduce")["jobs"]
        assert sorted(reduce_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, job_state="failed")["jobs"]
        assert sorted(map_failed_jobs) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, has_stderr=True)["jobs"]
        assert sorted(jobs_with_stderr) == sorted([job["id"] for job in res])

        res = list_jobs(op.id, has_stderr=False)["jobs"]
        assert sorted(jobs_without_stderr) == sorted([job["id"] for job in res])

        validate_address_filter(op, True, False, False)


import argparse
import os
import re

import yt
from yt.wrapper.cypress_commands import create, exists, list
from yt.wrapper.operation_commands import TimeWatcher, process_operation_unsuccesful_finish_state
from yt.wrapper.run_operation_commands import run_operation
from yt.wrapper.spec_builders import VanillaSpecBuilder

units = {"gb": 1024 * 1024 * 1024, "mb": 1024 * 1024, "kb": 1024, "bb": 1, "b": 1}


def parse_memory(memory_str):
    if memory_str is None:
        return None
    m = re.match("(\d+)(.*)", memory_str)
    value = long(m.group(1))
    unit = m.group(2).lower().strip()
    if len(unit) <= 1:
        unit = unit + "b"
    return value * units[unit]


def jmx_opts(port):
    return "-Dcom.sun.management.jmxremote " \
           "-Dcom.sun.management.jmxremote.port={} " \
           "-Dcom.sun.management.jmxremote.authenticate=false " \
           "-Dcom.sun.management.jmxremote.ssl=false".format(port)


parser = argparse.ArgumentParser(description="Spark over YT")
parser.add_argument("--id", required=True)
parser.add_argument("--worker-cores", required=True, type=int)
parser.add_argument("--worker-memory", required=True)
parser.add_argument("--worker-num", required=True, type=int)
parser.add_argument("--worker-opts", required=False)
parser.add_argument("--master-opts", required=False)
parser.add_argument("--proxy", required=False)
parser.add_argument("--version", required=False)
parser.add_argument("--working-dir", required=False)
parser.add_argument("--master-memory-limit", required=False)

args = parser.parse_args()

id = args.id
user = os.getenv("USER")
proxy = args.proxy or os.getenv("YT_PROXY")
spark_version = args.version or "2.4.4-bin-custom-spark"
spark_launcher_jar = "spark-yt-spark-launcher-assembly-0.0.1-SNAPSHOT.jar"
spark_base_path = "//home/sashbel/spark"
worker_memory = args.worker_memory
worker_num = args.worker_num
start_port = 27001
port_max_retries = 200
recovery_opts = "-Dspark.deploy.recoveryMode=CUSTOM " \
                "-Dspark.deploy.recoveryMode.factory=org.apache.spark.deploy.master.YtRecoveryModeFactory " \
                "-Dspark.deploy.yt.path=/home/sashbel/master"
worker_opts = args.worker_opts or "-Dspark.worker.cleanup.enabled=true " \
                                  "-Dspark.shuffle.service.enabled=true " \
                                  "-Dspark.hadoop.fs.yt.impl=ru.yandex.spark.yt.format.YtFileSystem " \
                                  "-Dspark.port.maxRetries={0} " \
                                  "-Dspark.shuffle.service.port=27000".format(port_max_retries)
master_opts = args.master_opts or "-Dspark.port.maxRetries={0} " \
                                  "-Dspark.hadoop.fs.yt.impl=ru.yandex.spark.yt.format.YtFileSystem " \
                                  "-Dspark.master.rest.enabled=true " \
                                  "-Dspark.master.rest.port={1}".format(port_max_retries, start_port)

working_dir = args.working_dir or "//home/{0}/spark-tmp".format(user)
master_memory_limit = parse_memory(args.master_memory_limit) or 2 * 1024 * 1024 * 1024
worker_memory_add = 2 * 1024 * 1024 * 1024

spark_name = "spark-{0}".format(spark_version)
unpack_tar = "tar --warning=no-unknown-keyword -xvf {0}.tgz".format(spark_name)
run_launcher = "/opt/jdk8/bin/java -Xmx512m -cp {0}".format(spark_launcher_jar)
master_main_class = "ru.yandex.spark.launcher.MasterLauncher"
worker_main_class = "ru.yandex.spark.launcher.WorkerLauncher"

file_paths = ["{0}/{1}.tgz".format(spark_base_path, spark_name),
              "{0}/{1}".format(spark_base_path, spark_launcher_jar)]

layer_paths = ["//home/sashbel/delta/jdk/layer_with_jdk_lastest.tar.gz",
               "//porto_layers/base/xenial/porto_layer_search_ubuntu_xenial_app_lastest.tar.gz"]

master_command = "{0} && {1} {2} --id {3} --operation-id $YT_OPERATION_ID " \
                 "--port {4} --web-ui-port {4} --opts \"'{5}'\"" \
    .format(unpack_tar, run_launcher, master_main_class, id, start_port, master_opts)

worker_command = "{0} && {1} {2} --id {3} --cores {4} --memory {5} " \
                 "--port {7} --web-ui-port {7} --opts \"'{6}'\"" \
    .format(unpack_tar, run_launcher, worker_main_class, id, args.worker_cores, worker_memory, worker_opts, start_port)

environment = {
    "JAVA_HOME": "/opt/jdk8",
    "SPARK_HOME": spark_name,
    "YT_PROXY": proxy,
    "SPARK_DISCOVERY_PATH": "{0}/instances".format(working_dir)
}

operation_spec = {
    "stderr_table_path": "{0}/logs/stderr_{1}".format(working_dir, id)
}

task_spec = {
    "restart_completed_jobs": True
}

with open(os.getenv("HOME") + "/.yt/token") as f:
    token = f.readline().strip()

secure_vault = {
    "YT_USER": user,
    "YT_TOKEN": token
}

spec_builder = \
    VanillaSpecBuilder() \
        .begin_task("master") \
            .job_count(1) \
            .file_paths(file_paths) \
            .command(master_command) \
            .memory_limit(master_memory_limit) \
            .cpu_limit(2) \
            .environment(environment) \
            .layer_paths(layer_paths) \
            .spec(task_spec) \
        .end_task() \
        .begin_task("workers") \
            .job_count(worker_num) \
            .file_paths(file_paths) \
            .command(worker_command) \
            .memory_limit(parse_memory(worker_memory) + worker_memory_add) \
            .cpu_limit(args.worker_cores) \
            .environment(environment) \
            .layer_paths(layer_paths) \
            .spec(task_spec) \
        .end_task() \
        .secure_vault(secure_vault) \
        .max_failed_job_count(1) \
        .max_stderr_count(150) \
        .spec(operation_spec)

yt.wrapper.config.config['proxy']['url'] = proxy
create("map_node", "{0}/logs".format(working_dir), recursive=True, ignore_existing=True)
op = run_operation(spec_builder, sync=False)


def wait_master_start():
    for state in op.get_state_monitor(TimeWatcher(1.0, 1.0, 0.0)):
        if state.is_running() and exists("{0}/instances/{1}/operation/{2}".format(working_dir, id, op.id)):
            return op
        elif state.is_unsuccessfully_finished():
            process_operation_unsuccesful_finish_state(op, state)
        else:
            op.printer(state)


wait_master_start()
master_address = list("{0}/instances/{1}/webui".format(working_dir, id))[0]

print("Spark Master's Web UI: http://{0}".format(master_address))

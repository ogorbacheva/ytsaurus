import os
import fcntl
import time

import yatest.common

YT_ABI = "19_4"

def prepare_yt_binaries(destination, source_prefix=""):
    programs = [("master", "master/bin"),
                ("node", "node/bin"),
                ("job-proxy", "job_proxy/bin"),
                ("exec", "bin/exec"),
                ("proxy", "rpc_proxy/bin"),
                ("http-proxy", "http_proxy/bin"),
                ("tools", "bin/tools"),
                ("scheduler", "scheduler/bin"),
                ("controller-agent", "controller_agent/bin")]
    for binary, server_dir in programs:
        binary_path = yatest.common.binary_path("{0}yt/{1}/yt/server/{2}/ytserver-{3}"
                                                .format(source_prefix, YT_ABI, server_dir, binary))
        os.symlink(binary_path, os.path.join(destination, "ytserver-" + binary))

    watcher_path = yatest.common.binary_path(source_prefix + "yt/python/yt/environment/bin/yt_env_watcher_make/yt_env_watcher")
    os.symlink(watcher_path, os.path.join(destination, "yt_env_watcher"))

    logrotate_path = yatest.common.binary_path(source_prefix + "infra/nanny/logrotate/logrotate")
    os.symlink(logrotate_path, os.path.join(destination, "logrotate"))

def prepare_yt_environment(destination):
    bin_dir = os.path.join(destination, "bin")
    lock_path = os.path.join(destination, "lock")
    prepared_path = os.path.join(destination, "prepared")

    try:
        lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR)
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except IOError:
        while not os.path.exists(prepared_path):
            time.sleep(0.1)
        return bin_dir

    if not os.path.exists(bin_dir):
        os.makedirs(bin_dir)

        prepare_yt_binaries(bin_dir)

    with open(prepared_path, "w"):
        pass

    return bin_dir

from .conftest import TESTS_LOCATION

from yt.common import update
import yt.subprocess_wrapper as subprocess
import yt.yson as yson

from yt.packages.six.moves import xrange

try:
    import yatest.common as ya_test_common
except ImportError:
    ya_test_common = None

import pytest

from copy import deepcopy
import os
import sys


class Cli(object):
    def __init__(self, grpc_address):
        if ya_test_common is not None:
            self.cli_execute = [ya_test_common.binary_path("yp/python/yp/bin/yp_make/yp")]
        else:
            self.cli_execute = [os.environ.get("PYTHON_BINARY", sys.executable),
                                os.path.normpath(os.path.join(TESTS_LOCATION, "../python/yp/bin/yp"))]
        self.env = update(deepcopy(os.environ), dict(YP_ADDRESS=grpc_address))
        self.set_config(dict(enable_ssl=False))

    def set_config(self, config):
        self.config = yson.dumps(config, yson_format="text")

    def _check_output(self, *args, **kwargs):
        return subprocess.check_output(*args, env=self.env, stderr=sys.stderr, **kwargs)

    def __call__(self, *args):
        return self._check_output(self.cli_execute + list(args) + ["--config", self.config])

def create_cli(yp_env):
    return Cli(yp_env.yp_instance.yp_client_grpc_address)


def create_pod(cli):
    pod_set_id = cli("create", "pod_set").strip()
    return cli("create", "pod", "--attributes", yson.dumps({"meta": {"pod_set_id": pod_set_id}})).strip()

def create_user(cli):
    return cli("create", "user").strip()


@pytest.mark.usefixtures("yp_env")
class TestCli(object):
    def test_common(self, yp_env):
        cli = create_cli(yp_env)

        pod_id = create_pod(cli)

        result = cli("get", "pod", pod_id, "--selector", "/status/agent/state", "--selector", "/meta/id")
        assert yson._loads_from_native_str(result) == ["unknown", pod_id]

        result = cli("select", "pod", "--filter", '[/meta/id] = "{}"'.format(pod_id), "--no-tabular")
        assert yson._loads_from_native_str(result) == [[]]

    def test_check_object_permission(self, yp_env):
        cli = create_cli(yp_env)

        pod_id = create_pod(cli)

        result = cli("check-object-permission", "pod", pod_id, "everyone", "read").strip()
        assert yson._loads_from_native_str(result) == dict(action="deny")

        result = cli("check-permission", "pod", pod_id, "root", "write").strip()
        assert yson._loads_from_native_str(result) == dict(action="allow")

        user_id = create_user(cli)
        yp_env.sync_access_control()

        result = cli("check-permission", "pod", pod_id, user_id, "read").strip()
        assert yson._loads_from_native_str(result) == dict(action="allow", object_type="schema", object_id="pod_set", subject_id="everyone")

    def test_get_object_access_allowed_for(self, yp_env):
        cli = create_cli(yp_env)

        pod_id = create_pod(cli)

        all_user_ids = ["root"]
        for _ in xrange(10):
            all_user_ids.append(create_user(cli))

        result_str = cli("get-object-access-allowed-for", "pod", pod_id, "read").strip()
        result = yson._loads_from_native_str(result_str)

        assert "user_ids" in result
        result["user_ids"].sort()

        assert result == dict(user_ids=sorted(all_user_ids))

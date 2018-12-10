from .conftest import Cli

from yp.local import ACTUAL_DB_VERSION, INITIAL_DB_VERSION

from yp.common import YtError

from yt.wrapper import ypath_join

import pytest

import os
import re


class YpAdminCli(Cli):
    def __init__(self):
        super(YpAdminCli, self).__init__("python/yp/bin", "yp_admin_make", "yp-admin")


def get_yt_proxy_address(yp_env):
    return yp_env.yp_instance.yt_instance.get_proxy_address()


ADMIN_CLI_TESTS_LOCAL_YT_OPTIONS = dict(start_proxy=True)


@pytest.mark.usefixtures("yp_env_configurable")
class TestAdminCli(object):
    LOCAL_YT_OPTIONS = ADMIN_CLI_TESTS_LOCAL_YT_OPTIONS

    def _get_db_version(self, yp_env, yp_path):
        cli = YpAdminCli()
        output = cli(
            "get-db-version",
            "--yt-proxy", get_yt_proxy_address(yp_env),
            "--yp-path", yp_path,
        )
        match = re.match("^Database version: ([0-9]+)$", output)
        assert match is not None
        return int(match.group(1))

    def test_get_db_version(self, yp_env_configurable):
        assert self._get_db_version(yp_env_configurable, "//yp") == ACTUAL_DB_VERSION

    def test_validate_db(self, yp_env_configurable):
        cli = YpAdminCli()
        output = cli(
            "validate-db",
            "--address", yp_env_configurable.yp_instance.yp_client_grpc_address,
            "--config", "{enable_ssl=%false}"
        )
        assert output == ""

    def test_prepare_udfs(self, yp_env_configurable):
        yt_client = yp_env_configurable.yp_instance.create_yt_client()
        yp_path = "//yp_prepare_udfs_test"
        yp_udfs_path = ypath_join(yp_path, "udfs")
        yt_client.create("map_node", yp_path)
        yt_client.create("map_node", yp_udfs_path)

        cli = YpAdminCli()
        output = cli(
            "prepare-udfs",
            "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
            "--yp-path", yp_path
        )
        assert output == ""

        assert "format_pod_current_state" in yt_client.list(yp_udfs_path)

    def test_init_and_migrate_db(self, yp_env_configurable):
        yt_client = yp_env_configurable.yp_instance.create_yt_client()
        yp_path = "//yp_init_db_test"
        yt_client.create("map_node", yp_path)

        cli = YpAdminCli()
        output = cli(
            "init-db",
            "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
            "--yp-path", yp_path
        )
        assert output == ""

        def assert_db(version):
            assert self._get_db_version(yp_env_configurable, yp_path) == version
            db_tables = yt_client.list(ypath_join(yp_path, "db"))
            assert all(x in db_tables for x in ("pods", "pod_sets", "node_segments"))

        assert_db(INITIAL_DB_VERSION)

        for current_db_version in range(INITIAL_DB_VERSION, ACTUAL_DB_VERSION):
            next_db_version = current_db_version + 1

            output = cli(
                "migrate-db",
                "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
                "--yp-path", yp_path,
                "--version", str(next_db_version),
                "--no-backup"
            )
            assert output == ""

            assert_db(next_db_version)

    def test_diff_db(self, yp_env_configurable):
        cli = YpAdminCli()
        output = cli(
            "diff-db",
            "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
            "--yp-path", "//yp",
            "--src-yp-path", "//yp"
        )
        assert output == ""

    def test_unmount_mount_db(self, yp_env_configurable):
        cli = YpAdminCli()
        output = cli(
            "unmount-db",
            "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
            "--yp-path", "//yp"
        )
        assert output == ""

        yp_client = yp_env_configurable.yp_client
        with pytest.raises(YtError):
            yp_client.select_objects("pod", selectors=["/meta/id"])

        output = cli(
            "mount-db",
            "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
            "--yp-path", "//yp"
        )
        assert output == ""

        yp_client.select_objects("pod", selectors=["/meta/id"])

    def test_dump_db(self, yp_env_configurable, tmpdir):
        cli = YpAdminCli()
        dump_dir_path = str(tmpdir.mkdir("yp_dump_db_test"))
        output = cli(
            "dump-db",
            "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
            "--yp-path", "//yp",
            "--dump-dir", dump_dir_path,
        )
        assert output == ""

        tables = os.listdir(os.path.join(dump_dir_path, "tables"))
        assert all(x in tables for x in ("pods", "resources", "groups"))


@pytest.mark.usefixtures("yp_env_configurable")
class TestAdminCliBackupRestore(object):
    LOCAL_YT_OPTIONS = ADMIN_CLI_TESTS_LOCAL_YT_OPTIONS
    START = False

    def test_backup_restore(self, yp_env_configurable):
        cli = YpAdminCli()
        for mode in ("backup", "restore"):
            output = cli(
                mode,
                "--yt-proxy", get_yt_proxy_address(yp_env_configurable),
                "--yp-path", "//yp",
                "--backup-path", "//yp.backup"
            )
            assert output == ""
        yp_env_configurable._start()
        yp_env_configurable.yp_client.select_objects("pod", selectors=["/meta/id"])

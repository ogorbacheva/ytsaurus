import pytest

from yt_env_setup import YTEnvSetup
from yt_commands import *
from time import sleep

##################################################################

class TestNodeTracker(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3

    DELTA_NODE_CONFIG = {
        "tags" : [ "config_tag1", "config_tag2" ]
    }

    def test_ban(self):
        nodes = ls("//sys/nodes")
        assert len(nodes) == 3

        test_node = nodes[0]
        assert get("//sys/nodes/%s/@state" % test_node) == "online"

        set_node_banned(test_node, True)
        set_node_banned(test_node, False)

    def test_resource_limits_overrides_defaults(self):
        node = ls("//sys/nodes")[0]
        assert get("//sys/nodes/{0}/@resource_limits_overrides".format(node)) == {}

    def test_disable_write_sessions(self):
        nodes = ls("//sys/nodes")
        assert len(nodes) == 3

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        for node in nodes:
            set("//sys/nodes/{0}/@disable_write_sessions".format(node), True) 

        with pytest.raises(YtError):
            write_table("//tmp/t", {"a" : "b"})

        for node in nodes:
            set("//sys/nodes/{0}/@disable_write_sessions".format(node), False) 

        sleep(1)

        # Now write must be successful.
        write_table("//tmp/t", {"a" : "b"})

    def test_disable_scheduler_jobs(self):
        nodes = ls("//sys/nodes")
        assert len(nodes) == 3

        test_node = nodes[0]
        assert get("//sys/nodes/{0}/@resource_limits/user_slots".format(test_node)) > 0
        set("//sys/nodes/{0}/@disable_scheduler_jobs".format(test_node), True) 

        sleep(1)

        assert get("//sys/nodes/{0}/@resource_limits/user_slots".format(test_node)) == 0

    def test_resource_limits_overrides_valiation(self):
        node = ls("//sys/nodes")[0]
        with pytest.raises(YtError): remove("//sys/nodes/{0}/@resource_limits_overrides".format(node))

    def test_user_tags_validation(self):
        node = ls("//sys/nodes")[0]
        with pytest.raises(YtError): set("//sys/nodes/{0}/@user_tags".format(node), 123)

    def test_user_tags_update(self):
        node = ls("//sys/nodes")[0]
        set("//sys/nodes/{0}/@user_tags".format(node), ["user_tag"])
        assert get("//sys/nodes/{0}/@user_tags".format(node)) == ["user_tag"]       
        assert "user_tag" in get("//sys/nodes/{0}/@tags".format(node))       

    def test_config_tags(self):
        for node in ls("//sys/nodes"):
            tags = get("//sys/nodes/{0}/@tags".format(node))
            assert "config_tag1" in tags
            assert "config_tag2" in tags

    def test_rack_tags(self):
        create_rack("r")
        node = ls("//sys/nodes")[0]
        assert "r" not in get("//sys/nodes/{0}/@tags".format(node))
        set("//sys/nodes/{0}/@rack".format(node), "r")
        assert "r" in get("//sys/nodes/{0}/@tags".format(node))
        remove("//sys/nodes/{0}/@rack".format(node))
        assert "r" not in get("//sys/nodes/{0}/@tags".format(node))

    def test_create_cluster_node(self):
        kwargs = {"type": "cluster_node"}
        with pytest.raises(YtError): execute_command("create", kwargs)

##################################################################

class TestNodeTrackerMulticell(TestNodeTracker):
    NUM_SECONDARY_MASTER_CELLS = 2

################################################################################

class TestNodesCreatedBanned(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    DEFER_NODE_START = True

    def test_new_nodes_created_banned(self):
        set("//sys/@config/node_tracker/total_node_statistics_update_period", 1000)

        assert get("//sys/nodes/@count") == 0
        assert get("//sys/nodes/@online_node_count") == 0

        assert not get("//sys/@config/node_tracker/ban_new_nodes")
        set("//sys/@config/node_tracker/ban_new_nodes", True)

        self.Env.start_nodes(sync=False)

        wait(lambda: get("//sys/nodes/@count") == 3)
        wait(lambda: get("//sys/nodes/@online_node_count") == 0)

        nodes = ls("//sys/nodes")
        assert len(nodes) == 3

        for node in nodes:
            assert get("//sys/nodes/{0}/@banned".format(node))
            ban_message = get("//sys/nodes/{0}/@ban_message".format(node))
            assert "banned" in ban_message and "provisionally" in ban_message
            assert get("//sys/nodes/{0}/@state".format(node)) == "offline"

        for node in nodes:
            set("//sys/nodes/{0}/@banned".format(node), False)

        self.Env.synchronize()

        wait(lambda: get("//sys/nodes/@online_node_count") == 3)

        create("table", "//tmp/t")

        write_table("//tmp/t", {"a" : "b"})
        read_table("//tmp/t")

class TestNodesCreatedBannedMulticell(TestNodesCreatedBanned):
    NUM_SECONDARY_MASTER_CELLS = 2

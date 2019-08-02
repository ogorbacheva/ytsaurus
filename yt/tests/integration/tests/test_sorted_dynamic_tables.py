import pytest
import __builtin__

from test_dynamic_tables import DynamicTablesBase

from yt_env_setup import wait, skip_if_rpc_driver_backend, parametrize_external, Restarter, NODES_SERVICE
from yt_commands import *
from yt.yson import YsonEntity, loads, dumps

from time import sleep
from random import randint, choice, sample
from string import ascii_lowercase

import random

from yt.environment.helpers import assert_items_equal

##################################################################

class TestSortedDynamicTablesBase(DynamicTablesBase):
    def _create_simple_table(self, path, **attributes):
        if "schema" not in attributes:
            attributes.update({"schema": [
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "string"}]
            })
        create_dynamic_table(path, **attributes)

    def _create_simple_static_table(self, path, **attributes):
        if "schema" not in attributes:
            attributes.update({"schema": make_schema([
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "string"}],
                unique_keys=True)
            })
        create("table", path, attributes=attributes)

    def _create_table_with_computed_column(self, path, **attributes):
        if "schema" not in attributes:
            attributes.update({"schema": [
                {"name": "key1", "type": "int64", "sort_order": "ascending"},
                {"name": "key2", "type": "int64", "sort_order": "ascending", "expression": "key1 * 100 + 3"},
                {"name": "value", "type": "string"}]
            })
        create_dynamic_table(path, **attributes)

    def _create_table_with_hash(self, path, **attributes):
        if "schema" not in attributes:
            attributes.update({"schema": [
                {"name": "hash", "type": "uint64", "expression": "farm_hash(key)", "sort_order": "ascending"},
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "string"}]
            })
        create_dynamic_table(path, **attributes)

    def _create_table_with_aggregate_column(self, path, aggregate="sum", **attributes):
        if "schema" not in attributes:
            attributes.update({"schema": [
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "time", "type": "int64"},
                {"name": "value", "type": "int64", "aggregate": aggregate}]
            })
        create_dynamic_table(path, **attributes)

    def _wait_for_in_memory_stores_preload(self, table):
        for tablet in get(table + "/@tablets"):
            tablet_id = tablet["tablet_id"]
            address = get_tablet_leader_address(tablet_id)
            def all_preloaded():
                orchid = self._find_tablet_orchid(address, tablet_id)
                if not orchid:
                    return False
                for store in orchid["eden"]["stores"].itervalues():
                    if store["store_state"] == "persistent" and store["preload_state"] != "complete":
                        return False
                for partition in orchid["partitions"]:
                    for store in partition["stores"].itervalues():
                        if store["preload_state"] != "complete":
                            return False
                return True
            wait(lambda: all_preloaded())

    def _reshard_with_retries(self, path, pivots):
        resharded = False
        for i in xrange(4):
            try:
                sync_unmount_table(path)
                sync_reshard_table(path, pivots)
                resharded = True
            except:
                pass
            sync_mount_table(path)
            if resharded:
                break
            sleep(5)
        assert resharded

##################################################################

class TestSortedDynamicTables(TestSortedDynamicTablesBase):
    DELTA_NODE_CONFIG = {
        "cluster_connection" : {
            "timestamp_provider" : {
                "update_period": 100
            }
        }
    }

    @authors("babenko", "ignat")
    def test_mount(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")

        sync_mount_table("//tmp/t")
        tablets = get("//tmp/t/@tablets")
        assert len(tablets) == 1
        tablet_id = tablets[0]["tablet_id"]
        cell_id = tablets[0]["cell_id"]

        tablet_ids = get("//sys/tablet_cells/" + cell_id + "/@tablet_ids")
        assert tablet_ids == [tablet_id]

    @authors("babenko")
    def test_unmount(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")

        sync_mount_table("//tmp/t")

        tablets = get("//tmp/t/@tablets")
        assert len(tablets) == 1

        tablet = tablets[0]
        assert tablet["pivot_key"] == []

        sync_mount_table("//tmp/t")
        sync_unmount_table("//tmp/t")

    @authors("savrus")
    def test_mount_unmount(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)
        actual = lookup_rows("//tmp/t", keys)
        assert_items_equal(actual, rows)

        sync_unmount_table("//tmp/t")
        with pytest.raises(YtError): lookup_rows("//tmp/t", keys)

        sync_mount_table("//tmp/t")
        actual = lookup_rows("//tmp/t", keys)
        assert_items_equal(actual, rows)

    @authors("gridem")
    def test_sorted_tablet_node_profiling(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        tablet_profiling = self._get_table_profiling("//tmp/t")
        select_profiling = self._get_profiling("//tmp/t")

        def get_all_counters(count_name):
            return (
                tablet_profiling.get_counter("lookup/" + count_name),
                tablet_profiling.get_counter("lookup/unmerged_" + count_name),
                tablet_profiling.get_counter("select/" + count_name),
                tablet_profiling.get_counter("select/unmerged_" + count_name),
                tablet_profiling.get_counter("write/" + count_name),
                tablet_profiling.get_counter("commit/" + count_name))

        assert get_all_counters("row_count") == (0, 0, 0, 0, 0, 0)
        assert get_all_counters("data_weight") == (0, 0, 0, 0, 0, 0)
        assert tablet_profiling.get_counter("lookup/cpu_time") == 0
        assert select_profiling.get_counter("select/cpu_time") == 0

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        wait(lambda: get_all_counters("row_count") == (0, 0, 0, 0, 1, 1) and \
                     get_all_counters("data_weight") == (0, 0, 0, 0, 10, 10) and \
                     tablet_profiling.get_counter("lookup/cpu_time") == 0 and \
                     select_profiling.get_counter("select/cpu_time") == 0)

        actual = lookup_rows("//tmp/t", keys)
        assert_items_equal(actual, rows)

        wait(lambda: get_all_counters("row_count") == (1, 1, 0, 0, 1, 1) and \
                     get_all_counters("data_weight") == (10, 25, 0, 0, 10, 10) and \
                     tablet_profiling.get_counter("lookup/cpu_time") > 0 and \
                     select_profiling.get_counter("select/cpu_time") == 0)

        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, rows)

        wait(lambda: get_all_counters("row_count") == (1, 1, 1, 1, 1, 1) and \
                     get_all_counters("data_weight") == (10, 25, 10, 25, 10, 10) and \
                     tablet_profiling.get_counter("lookup/cpu_time") > 0 and \
                     select_profiling.get_counter("select/cpu_time") > 0)

    @authors("gridem")
    def test_sorted_default_enabled_tablet_node_profiling(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t_unique_name")
        sync_mount_table("//tmp/t_unique_name")

        table_profiling = self._get_table_profiling("//tmp/t_unique_name")

        def get_all_counters(count_name):
            return (
                table_profiling.get_counter("lookup/" + count_name),
                table_profiling.get_counter("write/" + count_name),
                table_profiling.get_counter("commit/" + count_name))

        assert get_all_counters("row_count") == (0, 0, 0)
        assert get_all_counters("data_weight") == (0, 0, 0)
        assert table_profiling.get_counter("lookup/cpu_time") == 0

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t_unique_name", rows)

        wait(lambda: get_all_counters("row_count") == (0, 1, 1) and \
                     get_all_counters("data_weight") == (0, 10, 10) and \
                     table_profiling.get_counter("lookup/cpu_time") == 0)

        actual = lookup_rows("//tmp/t_unique_name", keys)
        assert_items_equal(actual, rows)

        wait(lambda: get_all_counters("row_count") == (1, 1, 1) and \
                     get_all_counters("data_weight") == (10, 10, 10) and \
                     table_profiling.get_counter("lookup/cpu_time") > 0)

    @authors("babenko", "ignat")
    def test_reshard_unmounted(self):
        sync_create_cells(1)
        create("table", "//tmp/t",attributes={
            "dynamic": True,
            "schema": [
                {"name": "k", "type": "int64", "sort_order": "ascending"},
                {"name": "l", "type": "uint64", "sort_order": "ascending"},
                {"name": "value", "type": "int64"}
            ]})

        sync_reshard_table("//tmp/t", [[]])
        assert self._get_pivot_keys("//tmp/t") == [[]]

        sync_reshard_table("//tmp/t", [[], [100]])
        assert self._get_pivot_keys("//tmp/t") == [[], [100]]

        with pytest.raises(YtError): reshard_table("//tmp/t", [[], []])
        assert self._get_pivot_keys("//tmp/t") == [[], [100]]

        sync_reshard_table("//tmp/t", [[100], [200]], first_tablet_index=1, last_tablet_index=1)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [200]]

        with pytest.raises(YtError): reshard_table("//tmp/t", [[101]], first_tablet_index=1, last_tablet_index=1)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [200]]

        with pytest.raises(YtError): reshard_table("//tmp/t", [[300]], first_tablet_index=3, last_tablet_index=3)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [200]]

        with pytest.raises(YtError): reshard_table("//tmp/t", [[100], [200]], first_tablet_index=1, last_tablet_index=1)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [200]]

        sync_reshard_table("//tmp/t", [[100], [150], [200]], first_tablet_index=1, last_tablet_index=2)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [150], [200]]

        with pytest.raises(YtError): reshard_table("//tmp/t", [[100], [100]], first_tablet_index=1, last_tablet_index=1)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [150], [200]]

        with pytest.raises(YtError): reshard_table("//tmp/t", [[], [100, 200]])
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [150], [200]]

    @authors("babenko", "levysotsky")
    def test_reshard_partly_unmounted(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_reshard_table("//tmp/t", [[], [100], [200], [300]])
        sync_mount_table("//tmp/t")
        with pytest.raises(YtError): reshard_table("//tmp/t", [[100], [250], [300]], first_tablet_index=1, last_tablet_index=3)
        sync_unmount_table("//tmp/t", first_tablet_index=1, last_tablet_index=3)
        sync_reshard_table("//tmp/t", [[100], [250], [300]], first_tablet_index=1, last_tablet_index=3)
        assert self._get_pivot_keys("//tmp/t") == [[], [100], [250], [300]]

    @authors("savrus", "levysotsky")
    def test_reshard_tablet_count(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_reshard_table("//tmp/t", [[], [1]])
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": i, "value": "A"*256} for i in xrange(2)])
        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")
        sync_unmount_table("//tmp/t")
        chunks = get("//tmp/t/@chunk_ids")
        assert len(chunks) == 2
        sync_reshard_table("//tmp/t", [[]])
        assert self._get_pivot_keys("//tmp/t") == [[]]
        sync_reshard_table("//tmp/t", 2)
        assert self._get_pivot_keys("//tmp/t") == [[], [1]]

    @authors("babenko")
    def test_force_unmount_on_remove(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        tablet_id = get("//tmp/t/@tablets/0/tablet_id")
        address = get_tablet_leader_address(tablet_id)
        assert self._find_tablet_orchid(address, tablet_id) is not None

        remove("//tmp/t")
        wait(lambda: self._find_tablet_orchid(address, tablet_id) is None)

    @authors("savrus")
    def test_lookup_repeated_keys(self):
        sync_create_cells(1)

        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in xrange(10)]
        insert_rows("//tmp/t", rows)

        keys = [{"key": i % 2} for i in xrange(10)]
        expected = [{"key": i % 2, "value": str(i % 2)} for i in xrange(10)]
        assert lookup_rows("//tmp/t", keys) == expected

        expected = [{"value": str(i % 2)} for i in xrange(10)]
        assert lookup_rows("//tmp/t", keys, column_names=["value"]) == expected

    @authors("sandello")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_lookup_versioned(self, optimize_for):
        sync_create_cells(1)

        self._create_simple_table("//tmp/t", optimize_for=optimize_for)
        sync_mount_table("//tmp/t")

        for prefix in ["a", "b"]:
            rows = [{"key": i, "value": prefix + ":" + str(i)} for i in xrange(10)]
            insert_rows("//tmp/t", rows)
            generate_timestamp()

        keys = [{"key": i} for i in xrange(10)]
        actual = lookup_rows("//tmp/t", keys, versioned=True)

        assert len(actual) == len(keys)

        for i, key in enumerate(keys):
            row = actual[i]
            assert "write_timestamps" in row.attributes
            assert len(row.attributes["write_timestamps"]) == 2
            assert "delete_timestamps" in row.attributes
            assert row["key"] == key["key"]
            assert len(row["value"]) == 2
            assert "%s" % row["value"][0] == "b:" + str(key["key"])
            assert "%s" % row["value"][1] == "a:" + str(key["key"])

    @authors("sandello")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_lookup_versioned_YT_6800(self, optimize_for):
        sync_create_cells(1)

        self._create_simple_table("//tmp/t",
            min_data_versions=0, min_data_ttl=0,
            max_data_versions=1000, max_data_ttl=1000000,
            optimize_for=optimize_for)
        sync_mount_table("//tmp/t")

        for prefix in ["a", "b", "c"]:
            rows = [{"key": i, "value": prefix + ":" + str(i)} for i in xrange(10)]
            insert_rows("//tmp/t", rows)
            generate_timestamp()

        keys = [{"key": i} for i in xrange(10)]
        actual = lookup_rows("//tmp/t", keys, versioned=True)

        assert len(actual) == len(keys)

        for i, key in enumerate(keys):
            row = actual[i]
            assert "write_timestamps" in row.attributes
            assert len(row.attributes["write_timestamps"]) == 3
            assert "delete_timestamps" in row.attributes
            assert row["key"] == key["key"]
            assert len(row["value"]) == 3
            assert "%s" % row["value"][0] == "c:" + str(key["key"])
            assert "%s" % row["value"][1] == "b:" + str(key["key"])
            assert "%s" % row["value"][2] == "a:" + str(key["key"])

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_lookup_versioned_filter(self, optimize_for):
        sync_create_cells(1)
        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value1", "type": "string"},
            {"name": "value2", "type": "string"}]
        create("table", "//tmp/t", attributes={
            "dynamic": True,
            "optimize_for": optimize_for,
            "schema": schema})

        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 0, "value1": "0"}], update=True)
        keys = [{"key": 0}]
        full_row = lookup_rows("//tmp/t", keys, versioned=True)[0]

        def _check(row):
            assert row.attributes["write_timestamps"] == full_row.attributes["write_timestamps"]
            assert row.attributes["delete_timestamps"] == full_row.attributes["delete_timestamps"]
            assert len(row) == 0

        actual = lookup_rows("//tmp/t", keys, column_names=["value2"], versioned=True)
        _check(actual[0])

        sync_flush_table("//tmp/t")

        actual = lookup_rows("//tmp/t", keys, column_names=["value2"], versioned=True)
        _check(actual[0])

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_lookup_versioned_filter_alter(self, optimize_for):
        sync_create_cells(1)
        schema1 = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value1", "type": "string"}]
        schema2 = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value1", "type": "string"},
            {"name": "value2", "type": "string"}]
        create("table", "//tmp/t", attributes={
            "dynamic": True,
            "optimize_for": optimize_for,
            "schema": schema1})

        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 0, "value1": "0"}], update=True)
        keys = [{"key": 0}]
        full_row = lookup_rows("//tmp/t", keys, versioned=True)[0]
        sync_unmount_table("//tmp/t")
        alter_table("//tmp/t", schema=schema2)
        sync_mount_table("//tmp/t")
        row = lookup_rows("//tmp/t", keys, column_names=["value2"], versioned=True)[0]
        assert row.attributes["write_timestamps"] == full_row.attributes["write_timestamps"]
        assert row.attributes["delete_timestamps"] == full_row.attributes["delete_timestamps"]
        assert len(row) == 0

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_lookup_versioned_retention(self, optimize_for):
        sync_create_cells(1)
        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value1", "type": "string"},
            {"name": "value2", "type": "string"},
            {"name": "value3", "type": "string"}]
        create("table", "//tmp/t", attributes={
            "dynamic": True,
            "optimize_for": optimize_for,
            "schema": schema})

        sync_mount_table("//tmp/t")
        for i in xrange(10):
            insert_rows("//tmp/t", [{"key": 0, "value1": str(i)}], update=True)
            insert_rows("//tmp/t", [{"key": 0, "value2": str(i)}], update=True)
        keys = [{"key": 0}]

        retention_config = {
            "min_data_ttl": 0,
            "max_data_ttl": 1000 * 60 * 10,
            "min_data_versions": 1,
            "max_data_versions": 1}

        full_row = lookup_rows("//tmp/t", keys, versioned=True, retention_config=retention_config)[0]
        assert len(full_row.attributes["write_timestamps"]) == 2
        assert len(full_row.attributes["delete_timestamps"]) == 0
        assert len(full_row) == 3

        def _check(row):
            assert row.attributes["write_timestamps"] == full_row.attributes["write_timestamps"]
            assert row.attributes["delete_timestamps"] == full_row.attributes["delete_timestamps"]
            assert len(row) == 0

        actual = lookup_rows("//tmp/t", keys, column_names=["value3"], versioned=True, retention_config=retention_config)
        _check(actual[0])

        sync_flush_table("//tmp/t")

        actual = lookup_rows("//tmp/t", keys, column_names=["value3"], versioned=True, retention_config=retention_config)
        _check(actual[0])

    @authors("ifsmirnov")
    def test_merge_rows_on_flush_removes_row(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@min_data_versions", 0)
        set("//tmp/t/@max_data_versions", 0)
        set("//tmp/t/@min_data_ttl", 0)
        set("//tmp/t/@max_data_ttl", 0)
        set("//tmp/t/@merge_rows_on_flush", True)
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": 1, "value": "a"}])
        assert select_rows("* from [//tmp/t]") == [{"key": 1, "value": "a"}]

        sync_unmount_table("//tmp/t")
        assert get("//tmp/t/@chunk_count") == 0

    @authors("savrus")
    def test_overflow_row_data_weight(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@enable_store_rotation", False)
        set("//tmp/t/@max_dynamic_store_row_data_weight", 100)
        sync_mount_table("//tmp/t")
        rows = [{"key": 0, "value": "A" * 100}]
        insert_rows("//tmp/t", rows)
        with pytest.raises(YtError):
            insert_rows("//tmp/t", rows)

    @authors("psushin")
    def test_read_invalid_limits(self):
        sync_create_cells(1)

        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        rows1 = [{"key": i, "value": str(i)} for i in xrange(10)]
        insert_rows("//tmp/t", rows1)
        sync_unmount_table("//tmp/t")

        with pytest.raises(YtError): read_table("//tmp/t[#5:]")
        with pytest.raises(YtError): read_table("<ranges=[{lower_limit={offset = 0};upper_limit={offset = 1}}]>//tmp/t")

    @authors("savrus")
    @pytest.mark.parametrize("erasure_codec", ["none", "reed_solomon_6_3", "lrc_12_2_2"])
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_read_table(self, optimize_for, erasure_codec):
        sync_create_cells(1)

        self._create_simple_table("//tmp/t", optimize_for=optimize_for, erasure_codec=erasure_codec)
        sync_mount_table("//tmp/t")

        rows1 = [{"key": i, "value": str(i)} for i in xrange(10)]
        insert_rows("//tmp/t", rows1)
        sync_freeze_table("//tmp/t")

        assert read_table("//tmp/t") == rows1
        assert get("//tmp/t/@chunk_count") == 1

        ts = generate_timestamp()

        sync_unfreeze_table("//tmp/t")
        rows2 = [{"key": i, "value": str(i+1)} for i in xrange(10)]
        insert_rows("//tmp/t", rows2)
        sync_unmount_table("//tmp/t")

        assert read_table("<timestamp=%s>//tmp/t" %(ts)) == rows1
        assert get("//tmp/t/@chunk_count") == 2

    @authors("savrus")
    def test_read_snapshot_lock(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        table_id = get("//tmp/t/@id")
        def _find_driver():
            for i in xrange(self.Env.secondary_master_cell_count):
                driver = get_driver(i + 1)
                if exists("#{0}".format(table_id), driver=driver):
                    return driver
            return None
        driver = _find_driver()

        def _multicell_lock(table, *args, **kwargs):
            lock(table, *args, **kwargs)
            def _check():
                locks = get("#{0}/@locks".format(table_id), driver=driver)
                if "tx" in kwargs:
                    for l in locks:
                        if l["transaction_id"] == kwargs["tx"]:
                            return True
                    return False
                else:
                    return len(locks) > 0
            wait(_check)

        def get_chunk_tree(path):
            root_chunk_list_id = get(path + "/@chunk_list_id")
            root_chunk_list = get("#" + root_chunk_list_id + "/@")
            tablet_chunk_lists = [get("#" + x + "/@") for x in root_chunk_list["child_ids"]]
            assert all([root_chunk_list_id in chunk_list["parent_ids"] for chunk_list in tablet_chunk_lists])
            # Validate against @chunk_count just to make sure that statistics arrive from secondary master to primary one.
            assert get(path + "/@chunk_count") == sum([len(chunk_list["child_ids"]) for chunk_list in tablet_chunk_lists])

            return root_chunk_list, tablet_chunk_lists

        def verify_chunk_tree_refcount(path, root_ref_count, tablet_ref_counts):
            root, tablets = get_chunk_tree(path)
            assert root["ref_counter"] == root_ref_count
            assert [tablet["ref_counter"] for tablet in tablets] == tablet_ref_counts

        verify_chunk_tree_refcount("//tmp/t", 1, [1])

        tx = start_transaction(timeout=60000, sticky=True)
        _multicell_lock("//tmp/t", mode="snapshot", tx=tx)
        verify_chunk_tree_refcount("//tmp/t", 2, [1])

        rows1 = [{"key": i, "value": str(i)} for i in xrange(0, 10, 2)]
        insert_rows("//tmp/t", rows1)
        sync_unmount_table("//tmp/t")
        verify_chunk_tree_refcount("//tmp/t", 1, [1])
        assert read_table("//tmp/t") == rows1
        assert read_table("//tmp/t", tx=tx) == []

        with pytest.raises(YtError):
            read_table("<timestamp={0}>//tmp/t".format(generate_timestamp()), tx=tx)

        abort_transaction(tx)
        verify_chunk_tree_refcount("//tmp/t", 1, [1])

        tx = start_transaction(timeout=60000, sticky=True)
        _multicell_lock("//tmp/t", mode="snapshot", tx=tx)
        verify_chunk_tree_refcount("//tmp/t", 2, [1])

        sync_reshard_table("//tmp/t", [[], [5]])
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 1])

        abort_transaction(tx)
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 1])

        tx = start_transaction(timeout=60000, sticky=True)
        _multicell_lock("//tmp/t", mode="snapshot", tx=tx)
        verify_chunk_tree_refcount("//tmp/t", 2, [1, 1])

        sync_mount_table("//tmp/t", first_tablet_index=0, last_tablet_index=0)

        rows2 = [{"key": i, "value": str(i)} for i in xrange(1, 5, 2)]
        insert_rows("//tmp/t", rows2)
        sync_unmount_table("//tmp/t")
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 2])
        assert_items_equal(read_table("//tmp/t"), rows1 + rows2)
        sleep(16)
        assert read_table("//tmp/t", tx=tx) == rows1

        sync_mount_table("//tmp/t")
        rows3 = [{"key": i, "value": str(i)} for i in xrange(5, 10, 2)]
        insert_rows("//tmp/t", rows3)
        sync_unmount_table("//tmp/t")
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 1])
        assert_items_equal(read_table("//tmp/t"), rows1 + rows2 + rows3)
        assert read_table("//tmp/t", tx=tx) == rows1

        abort_transaction(tx)
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 1])

        tx = start_transaction(timeout=60000, sticky=True)
        _multicell_lock("//tmp/t", mode="snapshot", tx=tx)
        verify_chunk_tree_refcount("//tmp/t", 2, [1, 1])

        sync_mount_table("//tmp/t")
        sync_compact_table("//tmp/t")
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 1])
        assert_items_equal(read_table("//tmp/t"), rows1 + rows2 + rows3)
        assert_items_equal(read_table("//tmp/t", tx=tx), rows1 + rows2 + rows3)

        abort_transaction(tx)
        verify_chunk_tree_refcount("//tmp/t", 1, [1, 1])

    @authors("savrus")
    def test_read_table_ranges(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", pivot_keys=[[], [5]])
        set("//tmp/t/@min_compaction_store_count", 5)
        sync_mount_table("//tmp/t")

        rows1 = [{"key": i, "value": str(i)} for i in xrange(10)]
        insert_rows("//tmp/t", rows1)
        sync_flush_table("//tmp/t")

        rows2 = [{"key": i, "value": str(i+1)} for i in xrange(1,5)]
        insert_rows("//tmp/t", rows2)
        sync_flush_table("//tmp/t")

        rows3 = [{"key": i, "value": str(i+2)} for i in xrange(5,9)]
        insert_rows("//tmp/t", rows3)
        sync_flush_table("//tmp/t")

        rows4 = [{"key": i, "value": str(i+3)} for i in xrange(0,3)]
        insert_rows("//tmp/t", rows4)
        sync_flush_table("//tmp/t")

        rows5 = [{"key": i, "value": str(i+4)} for i in xrange(7,10)]
        insert_rows("//tmp/t", rows5)
        sync_flush_table("//tmp/t")

        sync_freeze_table("//tmp/t")

        rows = []
        def update(new):
            def update_row(row):
                for r in rows:
                    if r["key"] == row["key"]:
                        r["value"] = row["value"]
                        return
                rows.append(row)
            for row in new:
                update_row(row)

        for r in [rows1, rows2, rows3, rows4, rows5]:
            update(r)

        assert read_table("//tmp/t[(2):(9)]") == rows[2:9]
        assert get("//tmp/t/@chunk_count") == 6

    @authors("savrus")
    @parametrize_external
    def test_read_table_when_chunk_crosses_tablet_boundaries(self, external):
        self._create_simple_static_table("//tmp/t", external=external)
        rows = [{"key": i, "value": str(i)} for i in xrange(6)]
        write_table("//tmp/t", rows)
        alter_table("//tmp/t", dynamic=True)

        def do_test():
            for i in xrange(6):
                assert read_table("//tmp/t[{0}:{1}]".format(i, i+1)) == rows[i:i+1]
            for i in xrange(0, 6, 2):
                assert read_table("//tmp/t[{0}:{1}]".format(i, i+2)) == rows[i:i+2]
            for i in xrange(1, 6, 2):
                assert read_table("//tmp/t[{0}:{1}]".format(i, i+2)) == rows[i:i+2]
        do_test()
        sync_reshard_table("//tmp/t", [[], [2], [4]])
        do_test()

    @authors("babenko", "levysotsky", "savrus")
    def test_write_table(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        with pytest.raises(YtError): write_table("//tmp/t", [{"key": 1, "value": 2}])

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_computed_columns(self, optimize_for):
        sync_create_cells(1)
        self._create_table_with_computed_column("//tmp/t", optimize_for=optimize_for)
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key1": 1, "value": "2"}])
        expected = [{"key1": 1, "key2": 103, "value": "2"}]
        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, expected)

        insert_rows("//tmp/t", [{"key1": 2, "value": "2"}])
        expected = [{"key1": 1, "key2": 103, "value": "2"}]
        actual = lookup_rows("//tmp/t", [{"key1" : 1}])
        assert_items_equal(actual, expected)
        expected = [{"key1": 2, "key2": 203, "value": "2"}]
        actual = lookup_rows("//tmp/t", [{"key1": 2}])
        assert_items_equal(actual, expected)

        delete_rows("//tmp/t", [{"key1": 1}])
        expected = [{"key1": 2, "key2": 203, "value": "2"}]
        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, expected)

        with pytest.raises(YtError): insert_rows("//tmp/t", [{"key1": 3, "key2": 3, "value": "3"}])
        with pytest.raises(YtError): lookup_rows("//tmp/t", [{"key1": 2, "key2": 203}])
        with pytest.raises(YtError): delete_rows("//tmp/t", [{"key1": 2, "key2": 203}])

        expected = []
        actual = lookup_rows("//tmp/t", [{"key1": 3}])
        assert_items_equal(actual, expected)

        expected = [{"key1": 2, "key2": 203, "value": "2"}]
        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, expected)

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_computed_hash(self, optimize_for):
        sync_create_cells(1)

        self._create_table_with_hash("//tmp/t", optimize_for=optimize_for)
        sync_mount_table("//tmp/t")

        row1 = [{"key": 1, "value": "2"}]
        insert_rows("//tmp/t", row1)
        actual = select_rows("key, value from [//tmp/t]")
        assert_items_equal(actual, row1)

        row2 = [{"key": 2, "value": "2"}]
        insert_rows("//tmp/t", row2)
        actual = lookup_rows("//tmp/t", [{"key": 1}], column_names=["key", "value"])
        assert_items_equal(actual, row1)
        actual = lookup_rows("//tmp/t", [{"key": 2}], column_names=["key", "value"])
        assert_items_equal(actual, row2)

        delete_rows("//tmp/t", [{"key": 1}])
        actual = select_rows("key, value from [//tmp/t]")
        assert_items_equal(actual, row2)

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_computed_column_update_consistency(self, optimize_for):
        sync_create_cells(1)
        create_dynamic_table("//tmp/t", optimize_for=optimize_for, schema=[
                {"name": "key1", "type": "int64", "expression": "key2", "sort_order": "ascending"},
                {"name": "key2", "type": "int64", "sort_order": "ascending"},
                {"name": "value1", "type": "string"},
                {"name": "value2", "type": "string"}]
            )
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key2": 1, "value1": "2"}])
        expected = [{"key1": 1, "key2": 1, "value1": "2", "value2" : YsonEntity()}]
        actual = lookup_rows("//tmp/t", [{"key2" : 1}])
        assert_items_equal(actual, expected)

        insert_rows("//tmp/t", [{"key2": 1, "value2": "3"}], update=True)
        expected = [{"key1": 1, "key2": 1, "value1": "2", "value2": "3"}]
        actual = lookup_rows("//tmp/t", [{"key2" : 1}])
        assert_items_equal(actual, expected)

        insert_rows("//tmp/t", [{"key2": 1, "value1": "4"}], update=True)
        expected = [{"key1": 1, "key2": 1, "value1": "4", "value2": "3"}]
        actual = lookup_rows("//tmp/t", [{"key2" : 1}])
        assert_items_equal(actual, expected)

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_aggregate_columns(self, optimize_for):
        sync_create_cells(1)
        self._create_table_with_aggregate_column("//tmp/t", optimize_for=optimize_for)
        sync_mount_table("//tmp/t")

        def verify_row(key, expected):
            actual = lookup_rows("//tmp/t", [{"key": key}])
            assert_items_equal(actual, expected)
            actual = select_rows("key, time, value from [//tmp/t]")
            assert_items_equal(actual, expected)

        @authors("savrus")
        def test_row(row, expected, **kwargs):
            insert_rows("//tmp/t", [row], **kwargs)
            verify_row(row["key"], [expected])

        def verify_after_flush(row):
            verify_row(row["key"], [row])
            assert_items_equal(read_table("//tmp/t"), [row])

        test_row({"key": 1, "time": 1, "value": 10}, {"key": 1, "time": 1, "value": 10}, aggregate=True)
        test_row({"key": 1, "time": 2, "value": 10}, {"key": 1, "time": 2, "value": 20}, aggregate=True)
        test_row({"key": 1, "time": 3, "value": 10}, {"key": 1, "time": 3, "value": 30}, aggregate=True)

        sync_flush_table("//tmp/t")

        verify_after_flush({"key": 1, "time": 3, "value": 30})
        test_row({"key": 1, "time": 4, "value": 10}, {"key": 1, "time": 4, "value": 40}, aggregate=True)
        test_row({"key": 1, "time": 5, "value": 10}, {"key": 1, "time": 5, "value": 50}, aggregate=True)
        test_row({"key": 1, "time": 6, "value": 10}, {"key": 1, "time": 6, "value": 60}, aggregate=True)

        sync_flush_table("//tmp/t")

        verify_after_flush({"key": 1, "time": 6, "value": 60})
        test_row({"key": 1, "time": 7, "value": 10}, {"key": 1, "time": 7, "value": 70}, aggregate=True)
        test_row({"key": 1, "time": 8, "value": 10}, {"key": 1, "time": 8, "value": 80}, aggregate=True)
        test_row({"key": 1, "time": 9, "value": 10}, {"key": 1, "time": 9, "value": 90}, aggregate=True)

        delete_rows("//tmp/t", [{"key": 1}])
        verify_row(1, [])
        test_row({"key": 1, "time": 10, "value": 10}, {"key": 1, "time": 10, "value": 10}, aggregate=True)
        test_row({"key": 1, "time": 11, "value": 10}, {"key": 1, "time": 11, "value": 20}, aggregate=True)
        test_row({"key": 1, "time": 12, "value": 10}, {"key": 1, "time": 12, "value": 30}, aggregate=True)

        sync_flush_table("//tmp/t")

        verify_after_flush({"key": 1, "time": 12, "value": 30})
        test_row({"key": 1, "time": 13, "value": 10}, {"key": 1, "time": 13, "value": 40}, aggregate=True)
        test_row({"key": 1, "time": 14, "value": 10}, {"key": 1, "time": 14, "value": 50}, aggregate=True)
        test_row({"key": 1, "time": 15, "value": 10}, {"key": 1, "time": 15, "value": 60}, aggregate=True)

        sync_flush_table("//tmp/t")

        verify_after_flush({"key": 1, "time": 15, "value": 60})
        delete_rows("//tmp/t", [{"key": 1}])
        verify_row(1, [])
        test_row({"key": 1, "time": 16, "value": 10}, {"key": 1, "time": 16, "value": 10}, aggregate=True)
        test_row({"key": 1, "time": 17, "value": 10}, {"key": 1, "time": 17, "value": 20}, aggregate=True)
        test_row({"key": 1, "time": 18, "value": 10}, {"key": 1, "time": 18, "value": 30}, aggregate=True)

        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")

        verify_after_flush({"key": 1, "time": 18, "value": 30})
        test_row({"key": 1, "time": 19, "value": 10}, {"key": 1, "time": 19, "value": 10})
        test_row({"key": 1, "time": 20, "value": 10}, {"key": 1, "time": 20, "value": 20}, aggregate=True)
        test_row({"key": 1, "time": 21, "value": 10}, {"key": 1, "time": 21, "value": 10})

        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")

        verify_after_flush({"key": 1, "time": 21, "value": 10})

    @authors("savrus")
    def test_aggregate_min_max(self):
        sync_create_cells(1)
        self._create_table_with_aggregate_column("//tmp/t", aggregate="min", optimize_for="scan")
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [
            {"key": 1, "time": 1, "value": 10},
            {"key": 2, "time": 1, "value": 20},
            {"key": 3, "time": 1}], aggregate=True)
        insert_rows("//tmp/t", [
            {"key": 1, "time": 2, "value": 30},
            {"key": 2, "time": 2, "value": 40},
            {"key": 3, "time": 2}], aggregate=True)
        assert_items_equal(select_rows("max(value) as max from [//tmp/t] group by 1"), [{"max": 20}])

    @authors("savrus")
    def test_aggregate_first(self):
        sync_create_cells(1)
        self._create_table_with_aggregate_column("//tmp/t", aggregate="first")
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": 1, "time": 1, "value": 10}], aggregate=True)
        insert_rows("//tmp/t", [{"key": 1, "time": 2, "value": 20}], aggregate=True)
        assert lookup_rows("//tmp/t", [{"key": 1}]) == [{"key": 1, "time": 2, "value": 10}]

    @authors("savrus")
    @pytest.mark.parametrize("aggregate", ["min", "max", "sum", "first"])
    def test_aggregate_update(self, aggregate):
        sync_create_cells(1)
        self._create_table_with_aggregate_column("//tmp/t", aggregate=aggregate)
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 1, "time": 1}], aggregate=True)
        assert lookup_rows("//tmp/t", [{"key": 1}]) == [{"key": 1, "time": 1, "value": None}]
        insert_rows("//tmp/t", [{"key": 1, "time": 2, "value": 10}], aggregate=True)
        assert lookup_rows("//tmp/t", [{"key": 1}]) == [{"key": 1, "time": 2, "value": 10}]
        insert_rows("//tmp/t", [{"key": 1, "time": 3}], aggregate=True)
        assert lookup_rows("//tmp/t", [{"key": 1}]) == [{"key": 1, "time": 3, "value": 10}]

    @authors("savrus")
    def test_aggregate_alter(self):
        sync_create_cells(1)
        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "time", "type": "int64"},
            {"name": "value", "type": "int64"}]
        create("table", "//tmp/t", attributes={"dynamic": True, "schema": schema})
        sync_mount_table("//tmp/t")

        def verify_row(key, expected):
            actual = lookup_rows("//tmp/t", [{"key": key}])
            assert_items_equal(actual, expected)
            actual = select_rows("key, time, value from [//tmp/t]")
            assert_items_equal(actual, expected)

        @authors("savrus")
        def test_row(row, expected, **kwargs):
            insert_rows("//tmp/t", [row], **kwargs)
            verify_row(row["key"], [expected])

        test_row({"key": 1, "time": 1, "value": 10}, {"key": 1, "time": 1, "value": 10}, aggregate=True)
        test_row({"key": 1, "time": 2, "value": 20}, {"key": 1, "time": 2, "value": 20}, aggregate=True)

        sync_unmount_table("//tmp/t")
        schema[2]["aggregate"] = "sum"
        alter_table("//tmp/t", schema=schema)
        sync_mount_table("//tmp/t")

        verify_row(1, [{"key": 1, "time": 2, "value": 20}])
        test_row({"key": 1, "time": 3, "value": 10}, {"key": 1, "time": 3, "value": 30}, aggregate=True)

    @authors("savrus")
    def test_aggregate_non_atomic(self):
        sync_create_cells(1)
        self._create_table_with_aggregate_column("//tmp/t", aggregate="sum", atomicity="none")
        sync_mount_table("//tmp/t")

        tx1 = start_transaction(type="tablet", atomicity="none")
        tx2 = start_transaction(type="tablet", atomicity="none")

        insert_rows("//tmp/t", [{"key": 1, "time": 1, "value": 10}], aggregate=True, atomicity="none", tx=tx1)
        insert_rows("//tmp/t", [{"key": 1, "time": 2, "value": 20}], aggregate=True, atomicity="none", tx=tx2)

        commit_transaction(tx1)
        commit_transaction(tx2)

        assert lookup_rows("//tmp/t", [{"key": 1}]) == [{"key": 1, "time": 2, "value": 30}]

    @pytest.mark.parametrize("merge_rows_on_flush, min_data_ttl, min_data_versions",
        [a + b for a in [(False,), (True,)] \
               for b in [(0, 0), (1, 10000)]])
    @authors("babenko")
    def test_aggregate_merge_rows_on_flush(self, merge_rows_on_flush, min_data_ttl, min_data_versions):
        sync_create_cells(1)
        self._create_table_with_aggregate_column("//tmp/t",
            merge_rows_on_flush=merge_rows_on_flush,
            min_data_ttl=min_data_ttl,
            min_data_versions=min_data_versions,
            max_data_ttl=1000000,
            max_data_versions=1)
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": 1, "time": 1, "value": 1000}], aggregate=False)
        delete_rows("//tmp/t", [{"key": 1}])
        insert_rows("//tmp/t", [{"key": 1, "time": 2, "value": 2000}], aggregate=True)
        delete_rows("//tmp/t", [{"key": 1}])
        insert_rows("//tmp/t", [{"key": 1, "time": 1, "value": 10}], aggregate=True)
        insert_rows("//tmp/t", [{"key": 1, "time": 2, "value": 20}], aggregate=True)

        assert_items_equal(select_rows("* from [//tmp/t]"), [{"key": 1, "time": 2, "value": 30}])

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), [{"key": 1, "time": 2, "value": 30}])

        insert_rows("//tmp/t", [{"key": 1, "time": 1, "value": 100}], aggregate=True)
        insert_rows("//tmp/t", [{"key": 1, "time": 2, "value": 200}], aggregate=True)

        assert_items_equal(select_rows("* from [//tmp/t]"), [{"key": 1, "time": 2, "value": 330}])

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), [{"key": 1, "time": 2, "value": 330}])

        sync_compact_table("//tmp/t")

        assert_items_equal(select_rows("* from [//tmp/t]"), [{"key": 1, "time": 2, "value": 330}])

    @authors("savrus")
    @pytest.mark.parametrize("aggregate", ["avg", "cardinality"])
    def test_invalid_aggregate(self, aggregate):
        sync_create_cells(1)
        with pytest.raises(YtError):
            self._create_table_with_aggregate_column("//tmp/t", aggregate=aggregate)

    @authors("lukyan")
    def test_transaction_locks(self):
        sync_create_cells(1)

        attributes = {"schema": [
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "a", "type": "int64", "lock": "a"},
                {"name": "b", "type": "int64", "lock": "b"},
                {"name": "c", "type": "int64", "lock": "c"}]
            }
        create_dynamic_table("//tmp/t", **attributes)
        sync_mount_table("//tmp/t")

        tx1 = start_transaction(type="tablet")
        tx2 = start_transaction(type="tablet")

        insert_rows("//tmp/t", [{"key": 1, "a": 1}], update=True, tx=tx1)
        lock_rows("//tmp/t", [{"key": 1}], locks=["a", "c"], tx=tx1, lock_type="shared_weak")
        insert_rows("//tmp/t", [{"key": 1, "b": 2}], update=True, tx=tx2)

        commit_transaction(tx1)
        commit_transaction(tx2)

        assert lookup_rows("//tmp/t", [{"key": 1}], column_names=["key", "a", "b"]) == [{"key": 1, "a": 1, "b": 2}]


        tx1 = start_transaction(type="tablet")
        tx2 = start_transaction(type="tablet")
        tx3 = start_transaction(type="tablet")

        insert_rows("//tmp/t", [{"key": 2, "a": 1}], update=True, tx=tx1)
        lock_rows("//tmp/t", [{"key": 2}], locks=["a", "c"], tx=tx1, lock_type="shared_weak")

        insert_rows("//tmp/t", [{"key": 2, "b": 2}], update=True, tx=tx2)
        lock_rows("//tmp/t", [{"key": 2}], locks=["c"], tx=tx2, lock_type="shared_weak")

        lock_rows("//tmp/t", [{"key": 2}], locks=["a"], tx=tx3, lock_type="shared_weak")

        commit_transaction(tx1)
        commit_transaction(tx2)

        with pytest.raises(YtError):
            commit_transaction(tx3)

        assert lookup_rows("//tmp/t", [{"key": 2}], column_names=["key", "a", "b"]) == [{"key": 2, "a": 1, "b": 2}]

        tx1 = start_transaction(type="tablet")
        tx2 = start_transaction(type="tablet")

        lock_rows("//tmp/t", [{"key": 3}], locks=["a"], tx=tx1, lock_type="shared_weak")
        insert_rows("//tmp/t", [{"key": 3, "a": 1}], update=True, tx=tx2)

        commit_transaction(tx2)

        with pytest.raises(YtError):
            commit_transaction(tx1)

        tx1 = start_transaction(type="tablet")
        tx2 = start_transaction(type="tablet")

        lock_rows("//tmp/t", [{"key": 3}], locks=["a"], tx=tx1, lock_type="shared_strong")
        insert_rows("//tmp/t", [{"key": 3, "a": 1}], update=True, tx=tx2)

        commit_transaction(tx1)

        with pytest.raises(YtError):
            commit_transaction(tx2)


    @authors("savrus")
    def test_reshard_data(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for="scan")
        sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in xrange(3)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        self._reshard_with_retries("//tmp/t", [[], [1]])
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        self._reshard_with_retries("//tmp/t", [[], [1], [2]])
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        self._reshard_with_retries("//tmp/t", [[]])
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

    @authors("savrus")
    def test_reshard_single_chunk(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", enable_compaction_and_partitioning=False)
        sync_mount_table("//tmp/t")

        def reshard(pivots):
            sync_unmount_table("//tmp/t")
            sync_reshard_table("//tmp/t", pivots)
            sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in xrange(3)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        reshard([[], [1]])
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        reshard([[], [1], [2]])
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

        reshard([[]])
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

    @authors("babenko", "savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_any_value_type(self, optimize_for):
        sync_create_cells(1)
        create("table", "//tmp/t1",
            attributes={
                "dynamic": True,
                "optimize_for" : optimize_for,
                "schema": [
                    {"name": "key", "type": "int64", "sort_order": "ascending"},
                    {"name": "value", "type": "any"}]
            })

        sync_mount_table("//tmp/t1")

        rows = [
            {"key": 11, "value": 100},
            {"key": 12, "value": False},
            {"key": 13, "value": True},
            {"key": 14, "value": 2**63 + 1 },
            {"key": 15, "value": 'stroka'},
            {"key": 16, "value": [1, {"attr": 3}, 4]},
            {"key": 17, "value": {"numbers": [0,1,42]}}]

        insert_rows("//tmp/t1", rows)
        actual = select_rows("* from [//tmp/t1]")
        assert_items_equal(actual, rows)
        actual = lookup_rows("//tmp/t1", [{"key": row["key"]} for row in rows])
        assert_items_equal(actual, rows)

    def _prepare_allowed(self, permission):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        create_user("u")
        set("//tmp/t/@inherit_acl", False)
        set("//tmp/t/@acl", [make_ace("allow", "u", permission)])

    def _prepare_denied(self, permission):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        create_user("u")
        set("//tmp/t/@acl", [make_ace("deny", "u", permission)])

    @authors("babenko")
    def test_select_allowed(self):
        self._prepare_allowed("read")
        insert_rows("//tmp/t", [{"key": 1, "value": "test"}])
        expected = [{"key": 1, "value": "test"}]
        actual = select_rows("* from [//tmp/t]", authenticated_user="u")
        assert_items_equal(actual, expected)

    @authors("babenko")
    def test_select_denied(self):
        self._prepare_denied("read")
        with pytest.raises(YtError): select_rows("* from [//tmp/t]", authenticated_user="u")

    @authors("babenko")
    def test_lookup_allowed(self):
        self._prepare_allowed("read")
        insert_rows("//tmp/t", [{"key": 1, "value": "test"}])
        expected = [{"key": 1, "value": "test"}]
        actual = lookup_rows("//tmp/t", [{"key" : 1}], authenticated_user="u")
        assert_items_equal(actual, expected)

    @authors("babenko")
    def test_lookup_denied(self):
        self._prepare_denied("read")
        insert_rows("//tmp/t", [{"key": 1, "value": "test"}])
        with pytest.raises(YtError): lookup_rows("//tmp/t", [{"key" : 1}], authenticated_user="u")

    @authors("babenko")
    def test_insert_allowed(self):
        self._prepare_allowed("write")
        insert_rows("//tmp/t", [{"key": 1, "value": "test"}], authenticated_user="u")
        expected = [{"key": 1, "value": "test"}]
        actual = lookup_rows("//tmp/t", [{"key" : 1}])
        assert_items_equal(actual, expected)

    @authors("babenko")
    def test_insert_denied(self):
        self._prepare_denied("write")
        with pytest.raises(YtError): insert_rows("//tmp/t", [{"key": 1, "value": "test"}], authenticated_user="u")

    @authors("babenko")
    def test_delete_allowed(self):
        self._prepare_allowed("write")
        insert_rows("//tmp/t", [{"key": 1, "value": "test"}])
        delete_rows("//tmp/t", [{"key": 1}], authenticated_user="u")
        expected = []
        actual = lookup_rows("//tmp/t", [{"key" : 1}])
        assert_items_equal(actual, expected)

    @authors("babenko")
    def test_delete_denied(self):
        self._prepare_denied("write")
        with pytest.raises(YtError): delete_rows("//tmp/t", [{"key": 1}], authenticated_user="u")

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_lookup_from_chunks(self, optimize_for):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for = optimize_for)

        pivots = [[]] + [[x] for x in range(100, 1000, 100)]
        sync_reshard_table("//tmp/t", pivots)
        assert self._get_pivot_keys("//tmp/t") == pivots

        sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in xrange(0, 1000, 2)]
        insert_rows("//tmp/t", rows)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        actual = lookup_rows("//tmp/t", [{'key': i} for i in xrange(0, 1000)])
        assert_items_equal(actual, rows)

        rows = [{"key": i, "value": str(i)} for i in xrange(1, 1000, 2)]
        insert_rows("//tmp/t", rows)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in xrange(0, 1000)]
        actual = lookup_rows("//tmp/t", [{'key': i} for i in xrange(0, 1000)])
        assert_items_equal(actual, rows)

        for tablet in xrange(10):
            path = "//tmp/t/@tablets/{0}/performance_counters/static_chunk_row_lookup_count".format(tablet)
            wait(lambda: get(path) > 0)
            assert get(path) == 200

    @authors("iskhakovt")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    @pytest.mark.parametrize("in_memory_mode", ["none", "compressed"])
    def test_data_weight_performance_counters(self, optimize_for, in_memory_mode):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for=optimize_for, in_memory_mode=in_memory_mode, dynamic_store_auto_flush_period=YsonEntity())
        sync_mount_table("//tmp/t")

        path = "//tmp/t/@tablets/0/performance_counters"

        insert_rows("//tmp/t", [{"key": 0, "value": "hello"}])

        wait(lambda: get(path + "/dynamic_row_write_data_weight_count") > 0)

        select_rows("* from [//tmp/t]")

        # Dynamic read must change, lookup must not change
        wait(lambda: get(path + "/dynamic_row_read_data_weight_count") > 0)
        assert get(path + "/dynamic_row_lookup_data_weight_count") == 0

        lookup_rows("//tmp/t", [{"key": 0}])

        # Dynamic read lookup change, read must not change
        wait(lambda: get(path + "/dynamic_row_lookup_data_weight_count") > 0)
        assert get(path + "/dynamic_row_read_data_weight_count") == get(path + "/dynamic_row_lookup_data_weight_count")

        # Static read/lookup must not change
        assert get(path + "/static_chunk_row_read_data_weight_count") == 0
        assert get(path + "/static_chunk_row_lookup_data_weight_count") == 0

        sync_flush_table("//tmp/t")

        select_rows("* from [//tmp/t]")

        # Static read must change, lookup must not change
        wait(lambda: get(path + "/static_chunk_row_read_data_weight_count") > 0)
        assert get(path + "/static_chunk_row_lookup_data_weight_count") == 0

        lookup_rows("//tmp/t", [{"key": 0}])

        # Static lookup must change, read must not change
        wait(lambda: get(path + "/static_chunk_row_lookup_data_weight_count") > 0)
        assert get(path + "/static_chunk_row_read_data_weight_count") == get(path + "/static_chunk_row_lookup_data_weight_count")

    @authors("babenko")
    def test_store_rotation(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")

        set("//tmp/t/@max_dynamic_store_row_count", 10)
        sync_mount_table("//tmp/t")

        tablet_id = get("//tmp/t/@tablets/0/tablet_id")
        address = get_tablet_leader_address(tablet_id)

        rows = [{"key": i, "value": str(i)} for i in xrange(10)]
        insert_rows("//tmp/t", rows)

        def check():
            tablet_data = self._find_tablet_orchid(address, tablet_id)
            return len(tablet_data["eden"]["stores"]) == 1 and \
                   len(tablet_data["partitions"]) == 1 and \
                   len(tablet_data["partitions"][0]["stores"]) == 1
        wait(lambda: check())

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    @pytest.mark.parametrize("mode", ["compressed", "uncompressed"])
    def test_in_memory(self, mode, optimize_for):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for=optimize_for)

        set("//tmp/t/@in_memory_mode", mode)
        set("//tmp/t/@max_dynamic_store_row_count", 10)
        sync_mount_table("//tmp/t")

        with pytest.raises(YtError):
            set("//tmp/t/@in_memory_mode", "none")

        tablet_id = get("//tmp/t/@tablets/0/tablet_id")
        address = get_tablet_leader_address(tablet_id)

        def _check_preload_state(state):
            sleep(1.0)
            tablet_data = self._find_tablet_orchid(address, tablet_id)
            assert len(tablet_data["eden"]["stores"]) == 1
            for partition in tablet_data["partitions"]:
                assert all(s["preload_state"] == state for _, s in partition["stores"].iteritems())
            actual_preload_completed = get("//tmp/t/@tablets/0/statistics/preload_completed_store_count")
            if state == "complete":
                assert actual_preload_completed >= 1
            else:
                assert actual_preload_completed == 0
            assert get("//tmp/t/@tablets/0/statistics/preload_pending_store_count") == 0
            assert get("//tmp/t/@tablets/0/statistics/preload_failed_store_count") == 0

        # Check preload after mount.
        rows = [{"key": i, "value": str(i)} for i in xrange(10)]
        keys = [{"key" : row["key"]} for row in rows]
        insert_rows("//tmp/t", rows)
        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")
        self._wait_for_in_memory_stores_preload("//tmp/t")
        _check_preload_state("complete")
        assert lookup_rows("//tmp/t", keys) == rows

        # Check preload after flush.
        rows = [{"key": i, "value": str(i + 1)} for i in xrange(10)]
        keys = [{"key" : row["key"]} for row in rows]
        insert_rows("//tmp/t", rows)
        sync_flush_table("//tmp/t")
        self._wait_for_in_memory_stores_preload("//tmp/t")
        _check_preload_state("complete")
        assert lookup_rows("//tmp/t", keys) == rows

        # Check preload after compaction.
        sync_compact_table("//tmp/t")
        self._wait_for_in_memory_stores_preload("//tmp/t")
        _check_preload_state("complete")
        assert lookup_rows("//tmp/t", keys) == rows

        # Disable in-memory mode
        sync_unmount_table("//tmp/t")
        set("//tmp/t/@in_memory_mode", "none")
        sync_mount_table("//tmp/t")
        _check_preload_state("none")
        assert lookup_rows("//tmp/t", keys) == rows

        # Re-enable in-memory mode
        sync_unmount_table("//tmp/t")
        set("//tmp/t/@in_memory_mode", mode)
        sync_mount_table("//tmp/t")
        self._wait_for_in_memory_stores_preload("//tmp/t")
        _check_preload_state("complete")
        assert lookup_rows("//tmp/t", keys) == rows

    @authors("savrus", "sandello")
    def test_lookup_hash_table(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")

        set("//tmp/t/@in_memory_mode", "uncompressed")
        set("//tmp/t/@enable_lookup_hash_table", True)
        set("//tmp/t/@max_dynamic_store_row_count", 10)
        sync_mount_table("//tmp/t")

        def _rows(i, j):
            return [{"key": k, "value": str(k)} for k in xrange(i, j)]

        def _keys(i, j):
            return [{"key": k} for k in xrange(i, j)]

        # check that we can insert rows
        insert_rows("//tmp/t", _rows(0, 5))
        assert lookup_rows("//tmp/t", _keys(0, 5)) == _rows(0, 5)

        # check that we can insert rows till capacity
        insert_rows("//tmp/t", _rows(5, 10))
        assert lookup_rows("//tmp/t", _keys(0, 10)) == _rows(0, 10)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")
        # ensure data is preloaded
        self._wait_for_in_memory_stores_preload("//tmp/t")

        # check that stores are rotated on-demand
        insert_rows("//tmp/t", _rows(10, 20))
        # ensure slot gets scanned
        sleep(3)
        insert_rows("//tmp/t", _rows(20, 30))
        assert lookup_rows("//tmp/t", _keys(10, 30)) == _rows(10, 30)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")
        # ensure data is preloaded
        self._wait_for_in_memory_stores_preload("//tmp/t")

        # check that we can delete rows
        delete_rows("//tmp/t", _keys(0, 10))
        assert lookup_rows("//tmp/t", _keys(0, 10)) == []

        # check that everything survives after recovery
        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")
        # ensure data is preloaded
        self._wait_for_in_memory_stores_preload("//tmp/t")
        assert lookup_rows("//tmp/t", _keys(0, 50)) == _rows(10, 30)

        # check that we can extend key
        sync_unmount_table("//tmp/t")
        alter_table("//tmp/t", schema=[
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "key2", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"}]);
        sync_mount_table("//tmp/t")
        # ensure data is preloaded
        self._wait_for_in_memory_stores_preload("//tmp/t")
        assert lookup_rows("//tmp/t", _keys(0, 50), column_names=["key", "value"]) == _rows(10, 30)

    @authors("babenko", "levysotsky")
    def test_update_key_columns_fail1(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        with pytest.raises(YtError): set("//tmp/t/@key_columns", ["key", "key2"])

    @authors("babenko")
    def test_update_key_columns_fail2(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        with pytest.raises(YtError): set("//tmp/t/@key_columns", ["key2", "key3"])

    @authors("babenko")
    def test_update_key_columns_fail3(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        with pytest.raises(YtError): set("//tmp/t/@key_columns", [])

    @authors("max42", "savrus")
    def test_alter_table_fails(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        # We have to insert at least one row to the table because any
        # valid schema can be set for an empty table without any checks.
        insert_rows("//tmp/t", [{"key": 1, "value": "test"}])
        sync_unmount_table("//tmp/t")
        with pytest.raises(YtError): alter_table("//tmp/t", schema=[
            {"name": "key1", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        with pytest.raises(YtError): alter_table("//tmp/t", schema=[
            {"name": "key", "type": "uint64", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        with pytest.raises(YtError): alter_table("//tmp/t", schema=[
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value1", "type": "string"}])

        self._create_table_with_computed_column("//tmp/t1")
        sync_mount_table("//tmp/t1")
        insert_rows("//tmp/t1", [{"key1": 1, "value": "test"}])
        sync_unmount_table("//tmp/t1")
        with pytest.raises(YtError): alter_table("//tmp/t1", schema=[
            {"name": "key1", "type": "int64", "sort_order": "ascending"},
            {"name": "key2", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        with pytest.raises(YtError): alter_table("//tmp/t1", schema=[
            {"name": "key1", "type": "int64", "expression": "key2 * 100 + 3", "sort_order": "ascending"},
            {"name": "key2", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        with pytest.raises(YtError): alter_table("//tmp/t1", schema=[
            {"name": "key1", "type": "int64", "sort_order": "ascending"},
            {"name": "key2", "type": "int64", "expression": "key1 * 100", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        with pytest.raises(YtError): alter_table("//tmp/t1", schema=[
            {"name": "key1", "type": "int64", "sort_order": "ascending"},
            {"name": "key2", "type": "int64", "expression": "key1 * 100 + 3", "sort_order": "ascending"},
            {"name": "key3", "type": "int64", "expression": "key1 * 100 + 3", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])

        create("table", "//tmp/t2", attributes={"schema": [
            {"name": "key", "type": "int64", "sort_order": "ascending"}]})
        with pytest.raises(YtError): alter_table("//tmp/t2", dynamic=True)
        alter_table("//tmp/t2", schema=[
            {"name": "key", "type": "any", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        with pytest.raises(YtError): alter_table("//tmp/t2", dynamic=True)

    @authors("babenko")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_update_key_columns_success(self, optimize_for):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for = optimize_for)

        sync_mount_table("//tmp/t")
        rows1 = [{"key": i, "value": str(i)} for i in xrange(100)]
        insert_rows("//tmp/t", rows1)
        sync_unmount_table("//tmp/t")

        alter_table("//tmp/t", schema=[
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "key2", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"}])
        sync_mount_table("//tmp/t")

        rows2 = [{"key": i, "key2": 0, "value": str(i)} for i in xrange(100)]
        insert_rows("//tmp/t", rows2)

        assert lookup_rows("//tmp/t", [{"key" : 77}]) == [{"key": 77, "key2": YsonEntity(), "value": "77"}]
        assert lookup_rows("//tmp/t", [{"key" : 77, "key2": 1}]) == []
        assert lookup_rows("//tmp/t", [{"key" : 77, "key2": 0}]) == [{"key": 77, "key2": 0, "value": "77"}]
        assert select_rows("sum(1) as s from [//tmp/t] where is_null(key2) group by 0") == [{"s": 100}]

    @authors("savrus")
    def test_create_table_with_invalid_schema(self):
        with pytest.raises(YtError):
            create("table", "//tmp/t", attributes={
                "dynamic": True,
                "schema": make_schema([{"name": "key", "type": "int64", "sort_order": "ascending"}])
                })
        assert not exists("//tmp/t")

    @authors("babenko")
    def test_atomicity_mode_should_match(self):
        def do(a1, a2):
            sync_create_cells(1)
            self._create_simple_table("//tmp/t", atomicity=a1)
            sync_mount_table("//tmp/t")
            rows = [{"key": i, "value": str(i)} for i in xrange(100)]
            with pytest.raises(YtError): insert_rows("//tmp/t", rows, atomicity=a2)
            remove("//tmp/t")

        do("full", "none")
        do("none", "full")

    @authors("babenko")
    @pytest.mark.parametrize("atomicity", ["full", "none"])
    def test_tablet_snapshots(self, atomicity):
        sync_create_cells(1)
        cell_id = ls("//sys/tablet_cells")[0]

        self._create_simple_table("//tmp/t", atomicity=atomicity)
        sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in xrange(100)]
        insert_rows("//tmp/t", rows, atomicity=atomicity)

        build_snapshot(cell_id=cell_id)

        snapshots = ls("//sys/tablet_cells/" + cell_id + "/snapshots")
        assert len(snapshots) == 1

        with Restarter(self.Env, NODES_SERVICE):
            # Wait to make sure all leases have expired
            time.sleep(3.0)

        wait_for_cells()

        # Wait to make sure all tablets are up
        time.sleep(3.0)

        keys = [{"key": i} for i in xrange(100)]
        actual = lookup_rows("//tmp/t", keys)
        assert_items_equal(actual, rows)

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_stress_tablet_readers(self, optimize_for):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for = optimize_for)
        sync_mount_table("//tmp/t")

        values = dict()

        def verify():
            expected = [{"key": key, "value": values[key]} for key in values.keys()]
            actual = select_rows("* from [//tmp/t]")
            assert_items_equal(actual, expected)

            keys = list(values.keys())[::2]
            for i in xrange(len(keys)):
                if i % 3 == 0:
                    j = (i * 34567) % len(keys)
                    keys[i], keys[j] = keys[j], keys[i]

            expected = [{"key": key, "value": values[key]} for key in keys]

            if len(keys) > 0:
                actual = select_rows("* from [//tmp/t] where key in (%s)" % ",".join([str(key) for key in keys]))
                assert_items_equal(actual, expected)

            actual = lookup_rows("//tmp/t", [{"key": key} for key in keys])
            assert actual == expected

        verify()

        rounds = 10
        items = 100

        for wave in xrange(1, rounds):
            rows = [{"key": i, "value": str(i + wave * 100)} for i in xrange(0, items, wave)]
            for row in rows:
                values[row["key"]] = row["value"]
            print_debug("Write rows ", rows)
            insert_rows("//tmp/t", rows)

            verify()

            pivots = ([[]] + [[x] for x in xrange(0, items, items / wave)]) if wave % 2 == 0 else [[]]
            self._reshard_with_retries("//tmp/t", pivots)

            verify()

            keys = sorted(list(values.keys()))[::(wave * 12345) % items]
            print_debug("Delete keys ", keys)
            rows = [{"key": key} for key in keys]
            delete_rows("//tmp/t", rows)
            for key in keys:
                values.pop(key)

            verify()

    @authors("ifsmirnov")
    @pytest.mark.parametrize("in_memory_mode", ["none", "uncompressed"])
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_stress_versioned_lookup(self, in_memory_mode, optimize_for):
        # This test checks that versioned lookup gives the same result for scan and lookup versioned formats.
        random.seed(12345)

        if in_memory_mode == "none" and optimize_for == "lookup":
            return

        schema = [
            {"name": "k1", "type": "int64", "sort_order": "ascending"},
            {"name": "k2", "type": "int64", "sort_order": "ascending"},
            {"name": "v1", "type": "int64"},
            {"name": "v2", "type": "int64"},
            {"name": "v3", "type": "int64"},
        ]

        delete_probability = 20 # percent
        value_probability = 70 # percent
        read_iters = 50
        lookup_iters = 50

        timestamps = []

        def random_write(table, keys):
            global timestamps

            for key in keys:
                for v in "v1", "v2", "v3":
                    if random.randint(0, 99) < value_probability:
                        key.update({v: random.randint(1, 10)})
            insert_rows(table, keys)

        def random_key():
            return {"k1": random.randint(1, 10), "k2": random.randint(1, 10)}

        sync_create_cells(1)
        self._create_simple_table("//tmp/expected", schema=schema, optimize_for="lookup")
        sync_mount_table("//tmp/expected")

        for i in range(read_iters):
            keys = [random_key() for i in range(5)]
            if random.randint(0, 99) < delete_probability:
                delete_rows("//tmp/expected", keys)
            else:
                random_write("//tmp/expected", keys)
            timestamps += [generate_timestamp()]

        sync_unmount_table("//tmp/expected")
        copy("//tmp/expected", "//tmp/actual")
        set("//tmp/actual/@optimize_for", optimize_for)
        set("//tmp/actual/@in_memory_mode", in_memory_mode)
        sync_mount_table("//tmp/expected")
        sync_mount_table("//tmp/actual")
        sync_compact_table("//tmp/actual")

        for i in range(lookup_iters):
            keys = [random_key() for i in range(5)]
            ts = random.choice(timestamps)
            for versioned in True, False:
                expected = lookup_rows("//tmp/expected", keys, versioned=versioned, timestamp=ts)
                actual = lookup_rows("//tmp/actual", keys, versioned=versioned, timestamp=ts)
                assert expected == actual

    @authors("ifsmirnov")
    def test_versioned_lookup_dynamic_store(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@enable_store_rotation", False)
        sync_mount_table("//tmp/t")

        timestamps = [generate_timestamp()]

        insert_rows("//tmp/t", [{"key": 1, "value": "a"}])
        timestamps += [lookup_rows("//tmp/t", [{"key": 1}], versioned=True)[0].attributes["write_timestamps"][0]]
        timestamps += [generate_timestamp()]

        delete_rows("//tmp/t", [{"key": 1}])
        timestamps += [lookup_rows("//tmp/t", [{"key": 1}], versioned=True)[0].attributes["delete_timestamps"][0]]
        timestamps += [generate_timestamp()]

        insert_rows("//tmp/t", [{"key": 1, "value": "b"}])
        timestamps += [lookup_rows("//tmp/t", [{"key": 1}], versioned=True)[0].attributes["write_timestamps"][0]]
        timestamps += [generate_timestamp()]

        delete_rows("//tmp/t", [{"key": 1}])
        timestamps += [lookup_rows("//tmp/t", [{"key": 1}], versioned=True)[0].attributes["delete_timestamps"][0]]
        timestamps += [generate_timestamp()]

        assert timestamps == sorted(timestamps)

        # Check one lookup explicitly.
        result = lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=timestamps[6])[0]
        assert result.attributes["write_timestamps"] == [timestamps[5], timestamps[1]]
        assert result.attributes["delete_timestamps"] == [timestamps[3]]
        value = result["value"]
        assert len(value) == 2
        assert value[0].attributes["timestamp"] == timestamps[5]
        assert str(value[0]) == "b"
        assert value[1].attributes["timestamp"] == timestamps[1]
        assert str(value[1]) == "a"

        # Check all lookups against chunk stores.
        actual = [
            lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=ts)
            for ts in timestamps]

        set("//tmp/t/@enable_store_rotation", True)
        remount_table("//tmp/t")
        sync_freeze_table("//tmp/t")

        expected = [
            lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=ts)
            for ts in timestamps]

        assert expected == actual

    @authors("ifsmirnov")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_versioned_lookup_unversioned_chunks(self, optimize_for):
        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "v1", "type": "int64"},
            {"name": "v2", "type": "int64"},
        ]

        create("table", "//tmp/t", attributes={
            "schema": make_schema(schema, strict=True, unique_keys=True),
            "optimize_for": optimize_for})

        timestamps = [generate_timestamp()]
        write_table("//tmp/t", [{"key": 0}])
        timestamps += [generate_timestamp()]
        write_table("<append=true>//tmp/t", [{"key": 1, "v1": 1}, {"key": 2, "v2": 2}], append=True)
        timestamps += [generate_timestamp()]
        write_table("<append=true>//tmp/t", [{"key": 3, "v1": 3, "v2": 4}], append=True)
        timestamps += [generate_timestamp()]

        assert len(read_table("//tmp/t"))

        sync_create_cells(1)
        alter_table("//tmp/t", dynamic=True)
        set("//tmp/t/@enable_compaction_and_partitioning", False)
        sync_mount_table("//tmp/t")

        def check(expected, actual):
            assert len(expected) == len(actual)
            for row in actual:
                key = row["key"]
                if row.attributes["write_timestamps"]:
                    assert key in expected
                    for column in "v1", "v2":
                        values = row.get(column)
                        if values is None:
                            assert column not in expected[key]
                            continue
                        assert len(values) == 1
                        if type(values[0]) == YsonEntity:
                            assert column not in expected[key]
                        else:
                            assert int(values[0]) == expected[key][column]
                else:
                    assert key not in expected

        expected = {}
        keys = [{"key": i} for i in range(4)]

        actual = lookup_rows("//tmp/t", keys, versioned=True, timestamp=timestamps.pop(0))
        check(expected, actual)

        expected[0] = {}
        actual = lookup_rows("//tmp/t", keys, versioned=True, timestamp=timestamps.pop(0))
        check(expected, actual)

        expected[1] = {"v1": 1}
        expected[2] = {"v2": 2}
        actual = lookup_rows("//tmp/t", keys, versioned=True, timestamp=timestamps.pop(0))
        check(expected, actual)

        expected[3] = {"v1": 3, "v2": 4}
        actual = lookup_rows("//tmp/t", keys, versioned=True, timestamp=timestamps.pop(0))
        check(expected, actual)

    @authors("ifsmirnov")
    def test_versioned_lookup_early_timestamp(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@enable_store_rotation", False)
        sync_mount_table("//tmp/t")

        ts = generate_timestamp()
        insert_rows("//tmp/t", [{"key": 1, "value": "a"}])

        assert lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=ts) == []

        set("//tmp/t/@enable_store_rotation", True)
        remount_table("//tmp/t")
        sync_flush_table("//tmp/t")

        assert lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=ts) == []

    @authors("ifsmirnov")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_versioned_lookup_early_timestamp_after_alter(self, optimize_for):
        sync_create_cells(1)

        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"},
        ]

        create("table", "//tmp/t", attributes={
            "schema": make_schema(schema, strict=True, unique_keys=True),
            "optimize_for": optimize_for})

        ts0 = generate_timestamp()
        write_table("//tmp/t", [{"key": 1, "value": "a"}])
        ts1 = generate_timestamp()

        alter_table("//tmp/t", dynamic=True)
        sync_mount_table("//tmp/t")

        assert lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=ts0) == []
        assert len(lookup_rows("//tmp/t", [{"key": 1}], versioned=True, timestamp=ts1)) == 1

    @authors("ifsmirnov")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    @pytest.mark.parametrize("in_memory_mode", ["none", "uncompressed"])
    def test_stress_chunk_view(self, optimize_for, in_memory_mode):
        random.seed(98765)

        set("//sys/@config/tablet_manager/tablet_balancer/enable_tablet_balancer", False)

        sync_create_cells(1)

        key_range=100
        num_writes_per_iteration=50
        num_deletes_per_iteration=10
        num_write_iterations=3
        num_lookup_iterations=30

        def random_row():
            return {"key": randint(1, key_range), "value": "".join(choice(ascii_lowercase) for i in range(5))}

        # Prepare both tables.
        self._create_simple_table("//tmp/t", optimize_for=optimize_for, in_memory_mode=in_memory_mode)
        set("//tmp/t/@enable_compaction_and_partitioning", False)
        sync_mount_table("//tmp/t")
        self._create_simple_table("//tmp/correct")
        sync_mount_table("//tmp/correct")

        if in_memory_mode != "none":
            self._wait_for_in_memory_stores_preload("//tmp/t")
            self._wait_for_in_memory_stores_preload("//tmp/correct")

        for iter in range(num_write_iterations):
            insert_keys = [random_row() for i in range(num_writes_per_iteration)]
            delete_keys = [{"key": randint(1, key_range)} for i in range(num_deletes_per_iteration)]

            insert_rows("//tmp/t", insert_keys)
            delete_rows("//tmp/t", delete_keys)
            insert_rows("//tmp/correct", insert_keys)
            delete_rows("//tmp/correct", delete_keys)

            sync_flush_table("//tmp/t")
            num_pivots = randint(0, 5)
            pivots = [[]] + [ [i] for i in sorted(sample(range(1, key_range+1), num_pivots)) ]
            sync_unmount_table("//tmp/t")
            sync_reshard_table("//tmp/t", pivots)
            sync_mount_table("//tmp/t")

            if in_memory_mode != "none":
                self._wait_for_in_memory_stores_preload("//tmp/t")

        for iter in range(num_lookup_iterations):
            # Lookup keys.
            keys = [{"key": randint(1, key_range)} for i in range(num_deletes_per_iteration)]

            expected = list(lookup_rows("//tmp/correct", keys))
            actual = list(lookup_rows("//tmp/t", keys))
            assert expected == actual

            # Lookup ranges.
            ranges_count = randint(1, 5)
            keys = sorted(sample(range(1, key_range+1), ranges_count * 2))
            query = '* from [{}] where ' + ' or '.join(
                    '({} <= key and key < {})'.format(l, r)
                    for l, r
                    in zip(keys[::2], keys[1::2]))
            expected = list(select_rows(query.format("//tmp/correct")))
            actual = list(select_rows(query.format("//tmp/t")))
            assert sorted(expected) == sorted(actual)

    @authors("ifsmirnov")
    def test_save_chunk_view_to_snapshot(self):
        [cell_id] = sync_create_cells(1)
        print_debug(get("//sys/cluster_nodes", attributes=["tablet_slots"]))
        print_debug(get("//sys/tablet_cell_bundles/default/@options"))
        set("//sys/@config/tablet_manager/tablet_cell_balancer/rebalance_wait_time", 500)
        set("//sys/@config/tablet_manager/tablet_cell_balancer/enable_tablet_cell_balancer", True)

        self._create_simple_table("//tmp/t")
        set("//tmp/t/@enable_compaction_and_partitioning", False)
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": i, "value": str(i)} for i in range(2)])
        sync_unmount_table("//tmp/t")
        sync_reshard_table("//tmp/t", [[], [1]])
        sync_mount_table("//tmp/t")

        print_debug(get("//sys/tablet_cells/{}/@peers".format(cell_id)))
        build_snapshot(cell_id=cell_id)

        peer = get("//sys/tablet_cells/{}/@peers/0/address".format(cell_id))
        set("//sys/cluster_nodes/{}/@banned".format(peer), True)

        wait(lambda: get("//sys/tablet_cells/{}/@health".format(cell_id)) == "good")

        assert list(lookup_rows("//tmp/t", [{"key": 0}])) == [{"key": 0, "value": "0"}]
        assert list(lookup_rows("//tmp/t", [{"key": 1}])) == [{"key": 1, "value": "1"}]

    @authors("ifsmirnov")
    def test_partition_balancer_chunk_view(self):
        [cell_id] = sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@chunk_writer", {"block_size": 64})
        set("//tmp/t/@compression_codec", "none")
        set("//tmp/t/@tablet_balancer_config/enable_auto_reshard", False)
        set("//tmp/t/@enable_compaction_and_partitioning", False)
        set("//tmp/t/@max_partition_data_size", 640)
        set("//tmp/t/@desired_partition_data_size", 512)
        set("//tmp/t/@min_partition_data_size", 256)
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": i, "value": str(i)} for i in xrange(20)])
        sync_unmount_table("//tmp/t")
        sync_reshard_table("//tmp/t", [[], [1], [18]])
        sync_mount_table("//tmp/t")


        tablet_id = get("//tmp/t/@tablets/1/tablet_id")
        address = get_tablet_leader_address(tablet_id)
        orchid = self._find_tablet_orchid(address, tablet_id)
        assert len(orchid["partitions"]) == 1

        build_snapshot(cell_id=cell_id)

        peer = get("//sys/tablet_cells/{}/@peers/0/address".format(cell_id))
        set("//sys/cluster_nodes/{}/@banned".format(peer), True)
        wait(lambda: get("//sys/tablet_cells/{}/@health".format(cell_id)) == "good")

        set("//tmp/t/@enable_compaction_and_partitioning", True)
        remount_table("//tmp/t")
        address = get_tablet_leader_address(tablet_id)
        wait(lambda : len(self._find_tablet_orchid(address, tablet_id)["partitions"]) > 1)

    @authors("babenko")
    def test_rff_requires_async_last_committed(self):
        create_tablet_cell_bundle("b", attributes={"options": {"peer_count" : 3}})
        sync_create_cells(1, tablet_cell_bundle="b")
        self._create_simple_table("//tmp/t", optimize_for = "scan", tablet_cell_bundle="b")
        sync_mount_table("//tmp/t")

        keys = [{"key": 1}]
        with pytest.raises(YtError): lookup_rows("//tmp/t", keys, read_from="follower")

    @authors("babenko")
    def test_rff_when_only_leader_exists(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        assert lookup_rows("//tmp/t", keys, read_from="follower") == rows

    @authors("babenko")
    def test_rff_lookup(self):
        create_tablet_cell_bundle("b", attributes={"options": {"peer_count" : 3}})
        sync_create_cells(1, tablet_cell_bundle="b")
        self._create_simple_table("//tmp/t", optimize_for = "scan", tablet_cell_bundle="b")
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        wait(lambda: lookup_rows("//tmp/t", keys, read_from="follower", timestamp=AsyncLastCommittedTimestamp) == rows)

    @authors("babenko")
    def test_lookup_with_backup(self):
        create_tablet_cell_bundle("b", attributes={"options": {"peer_count" : 3}})
        sync_create_cells(1, tablet_cell_bundle="b")
        self._create_simple_table("//tmp/t", tablet_cell_bundle="b")
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        sleep(1.0)
        for delay in xrange(0, 10):
            assert lookup_rows("//tmp/t", keys, read_from="follower", backup_request_delay=delay, timestamp=AsyncLastCommittedTimestamp) == rows

    @authors("babenko")
    def test_erasure(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", optimize_for = "scan")
        set("//tmp/t/@erasure_codec", "lrc_12_2_2")
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        insert_rows("//tmp/t", rows)

        sync_unmount_table("//tmp/t")

        chunk_id = get_singular_chunk_id("//tmp/t")

        assert get("#" + chunk_id + "/@erasure_codec") == "lrc_12_2_2"

        sync_mount_table("//tmp/t")
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

    @authors("savrus")
    def test_keep_missing_rows(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}, {"key": 2}]
        expect_rows = rows + [None]
        insert_rows("//tmp/t", rows)
        actual = lookup_rows("//tmp/t", keys, keep_missing_rows=True);
        assert len(actual) == 2
        assert_items_equal(rows[0], actual[0])
        assert actual[1] == None

    @authors("savrus", "levysotsky")
    def test_chunk_statistics(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 1, "value": "1"}])
        sync_unmount_table("//tmp/t")
        chunk_list_id = get("//tmp/t/@chunk_list_id")
        statistics1 = get("#" + chunk_list_id + "/@statistics")
        sync_mount_table("//tmp/t")
        sync_compact_table("//tmp/t")
        statistics2 = get("#" + chunk_list_id + "/@statistics")
        # Disk space is not stable since it includes meta
        del statistics1["regular_disk_space"]
        del statistics2["regular_disk_space"]
        assert statistics1 == statistics2

    @authors("babenko")
    def test_tablet_statistics(self):
        cell_ids = sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 1, "value": "1"}])
        sync_freeze_table("//tmp/t")
        def check_statistics(statistics):
            return statistics["tablet_count"] == 1 and \
                   statistics["tablet_count_per_memory_mode"]["none"] == 1 and \
                   statistics["chunk_count"] == get("//tmp/t/@chunk_count") and \
                   statistics["uncompressed_data_size"] == get("//tmp/t/@uncompressed_data_size") and \
                   statistics["compressed_data_size"] == get("//tmp/t/@compressed_data_size") and \
                   statistics["disk_space"] == get("//tmp/t/@resource_usage/disk_space") and \
                   statistics["disk_space_per_medium"]["default"] == get("//tmp/t/@resource_usage/disk_space_per_medium/default")

        tablet_statistics = get("//tmp/t/@tablet_statistics")
        assert tablet_statistics["overlapping_store_count"] == tablet_statistics["store_count"]
        assert check_statistics(tablet_statistics)

        wait(lambda: check_statistics(get("#{0}/@total_statistics".format(cell_ids[0]))))

    @authors("savrus")
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    def test_timestamp_access(self, optimize_for):
        sync_create_cells(3)
        self._create_simple_table("//tmp/t", optimize_for = optimize_for)
        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", rows)

        assert lookup_rows("//tmp/t", keys, timestamp=MinTimestamp) == []
        assert select_rows("* from [//tmp/t]", timestamp=MinTimestamp) == []

    @authors("savrus")
    def test_column_groups(self):
        sync_create_cells(1)
        create("table", "//tmp/t",
            attributes={
                "dynamic": True,
                "optimize_for": "scan",
                "schema": [
                    {"name": "key", "type": "int64", "sort_order": "ascending", "group": "a"},
                    {"name": "value", "type": "string", "group": "a"}]
            })
        sync_mount_table("//tmp/t")

        rows = [{"key": i, "value": str(i)} for i in range(2)]
        keys = [{"key": row["key"]} for row in rows]
        insert_rows("//tmp/t", rows)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert lookup_rows("//tmp/t", keys) == rows
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)

    @authors("babenko", "levysotsky")
    def test_freeze_empty(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        sync_freeze_table("//tmp/t")
        with pytest.raises(YtError): insert_rows("//tmp/t", [{"key": 0}])
        sync_unfreeze_table("//tmp/t")
        sync_unmount_table("//tmp/t")

    @authors("babenko", "levysotsky")
    def test_freeze_nonempty(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        rows = [{"key": 0, "value": "test"}]
        insert_rows("//tmp/t", rows)
        sync_freeze_table("//tmp/t")
        wait(lambda: get("//tmp/t/@expected_tablet_state") == "frozen")
        assert get("//tmp/t/@chunk_count") == 1
        assert select_rows("* from [//tmp/t]") == rows
        sync_unfreeze_table("//tmp/t")
        assert select_rows("* from [//tmp/t]") == rows
        sync_unmount_table("//tmp/t")

    @authors("babenko", "levysotsky")
    def test_unmount_frozen(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        rows = [{"key": 0}]
        insert_rows("//tmp/t", rows)
        sync_freeze_table("//tmp/t")
        sync_unmount_table("//tmp/t")

    @authors("babenko", "levysotsky")
    def test_mount_as_frozen(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        rows = [{"key": 1, "value": "2"}]
        insert_rows("//tmp/t", rows)
        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t", freeze=True)
        assert select_rows("* from [//tmp/t]") == rows

    @authors("savrus")
    def test_access_to_frozen(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")
        rows = [{"key": 1, "value": "2"}]
        insert_rows("//tmp/t", rows)
        sync_freeze_table("//tmp/t")
        assert lookup_rows("//tmp/t", [{"key": 1}]) == rows
        assert select_rows("* from [//tmp/t]") == rows
        with pytest.raises(YtError): insert_rows("//tmp/t", rows)

    def _prepare_copy(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t1")
        sync_reshard_table("//tmp/t1", [[]] + [[i * 100] for i in xrange(10)])

    @authors("babenko")
    def test_copy_failure(self):
        self._prepare_copy()
        sync_mount_table("//tmp/t1")
        with pytest.raises(YtError): copy("//tmp/t1", "//tmp/t2")

    @authors("babenko")
    def test_copy_empty(self):
        self._prepare_copy()
        copy("//tmp/t1", "//tmp/t2")

        root_chunk_list_id1 = get("//tmp/t1/@chunk_list_id")
        root_chunk_list_id2 = get("//tmp/t2/@chunk_list_id")
        assert root_chunk_list_id1 != root_chunk_list_id2

        assert get("#{0}/@ref_counter".format(root_chunk_list_id1)) == 1
        assert get("#{0}/@ref_counter".format(root_chunk_list_id2)) == 1

        child_ids1 = get("#{0}/@child_ids".format(root_chunk_list_id1))
        child_ids2 = get("#{0}/@child_ids".format(root_chunk_list_id2))
        assert child_ids1 == child_ids2

        for child_id in child_ids1:
            assert get("#{0}/@ref_counter".format(child_id)) == 2
            assert_items_equal(get("#{0}/@owning_nodes".format(child_id)), ["//tmp/t1", "//tmp/t2"])

    @pytest.mark.parametrize("unmount_func, mount_func, unmounted_state", [
        [sync_unmount_table, sync_mount_table, "unmounted"],
        [sync_freeze_table, sync_unfreeze_table, "frozen"]])
    @authors("babenko", "levysotsky")
    def test_copy_simple(self, unmount_func, mount_func, unmounted_state):
        self._prepare_copy()
        sync_mount_table("//tmp/t1")
        rows = [{"key": i * 100 - 50} for i in xrange(10)]
        insert_rows("//tmp/t1", rows)
        unmount_func("//tmp/t1")
        copy("//tmp/t1", "//tmp/t2")
        assert get("//tmp/t1/@tablet_state") == unmounted_state
        assert get("//tmp/t2/@tablet_state") == "unmounted"
        mount_func("//tmp/t1")
        sync_mount_table("//tmp/t2")
        assert_items_equal(select_rows("key from [//tmp/t1]"), rows)
        assert_items_equal(select_rows("key from [//tmp/t2]"), rows)

    @pytest.mark.parametrize("unmount_func, mount_func, unmounted_state", [
        [sync_unmount_table, sync_mount_table, "unmounted"],
        [sync_freeze_table, sync_unfreeze_table, "frozen"]])
    @authors("babenko")
    def test_copy_and_fork(self, unmount_func, mount_func, unmounted_state):
        self._prepare_copy()
        sync_mount_table("//tmp/t1")
        rows = [{"key": i * 100 - 50} for i in xrange(10)]
        insert_rows("//tmp/t1", rows)
        unmount_func("//tmp/t1")
        copy("//tmp/t1", "//tmp/t2")
        assert get("//tmp/t1/@tablet_state") == unmounted_state
        assert get("//tmp/t2/@tablet_state") == "unmounted"
        mount_func("//tmp/t1")
        sync_mount_table("//tmp/t2")
        ext_rows1 = [{"key": i * 100 - 51} for i in xrange(10)]
        ext_rows2 = [{"key": i * 100 - 52} for i in xrange(10)]
        insert_rows("//tmp/t1", ext_rows1)
        insert_rows("//tmp/t2", ext_rows2)
        assert_items_equal(select_rows("key from [//tmp/t1]"), rows + ext_rows1)
        assert_items_equal(select_rows("key from [//tmp/t2]"), rows + ext_rows2)

    @authors("babenko")
    def test_copy_and_compact(self):
        self._prepare_copy()
        sync_mount_table("//tmp/t1")
        rows = [{"key": i * 100 - 50} for i in xrange(10)]
        insert_rows("//tmp/t1", rows)
        sync_unmount_table("//tmp/t1")
        copy("//tmp/t1", "//tmp/t2")
        sync_mount_table("//tmp/t1")
        sync_mount_table("//tmp/t2")

        original_chunk_ids1 = __builtin__.set(get("//tmp/t1/@chunk_ids"))
        original_chunk_ids2 = __builtin__.set(get("//tmp/t2/@chunk_ids"))
        assert original_chunk_ids1 == original_chunk_ids2

        ext_rows1 = [{"key": i * 100 - 51} for i in xrange(10)]
        ext_rows2 = [{"key": i * 100 - 52} for i in xrange(10)]
        insert_rows("//tmp/t1", ext_rows1)
        insert_rows("//tmp/t2", ext_rows2)

        sync_compact_table("//tmp/t1")
        sync_compact_table("//tmp/t2")

        compacted_chunk_ids1 = __builtin__.set(get("//tmp/t1/@chunk_ids"))
        compacted_chunk_ids2 = __builtin__.set(get("//tmp/t2/@chunk_ids"))
        assert len(compacted_chunk_ids1.intersection(compacted_chunk_ids2)) == 0

        assert_items_equal(select_rows("key from [//tmp/t1]"), rows + ext_rows1)
        assert_items_equal(select_rows("key from [//tmp/t2]"), rows + ext_rows2)

    @authors("savrus")
    @skip_if_rpc_driver_backend
    @parametrize_external
    def test_mount_static_table_fails(self, external):
        sync_create_cells(1)
        self._create_simple_static_table("//tmp/t", external=external, schema=[
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "string"}])
        assert not get("//tmp/t/@schema/@unique_keys")
        with pytest.raises(YtError): alter_table("//tmp/t", dynamic=True)

    @parametrize_external
    @pytest.mark.parametrize("optimize_for", ["lookup", "scan"])
    @pytest.mark.parametrize("in_memory_mode, enable_lookup_hash_table", [
        ["none", False],
        ["compressed", False],
        ["uncompressed", True]])
    @authors("savrus")
    def test_mount_static_table(self, in_memory_mode, enable_lookup_hash_table, optimize_for, external):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", dynamic=False, optimize_for=optimize_for, external=external,
            schema=make_schema([
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "string"},
                {"name": "avalue", "type": "int64", "aggregate": "sum"}],
                unique_keys=True))
        rows = [{"key": i, "value": str(i), "avalue": 1} for i in xrange(2)]
        keys = [{"key": row["key"]} for row in rows] + [{"key": -1}, {"key": 1000}]

        start_ts = generate_timestamp()
        write_table("//tmp/t", rows)
        alter_table("//tmp/t", dynamic=True)
        set("//tmp/t/@in_memory_mode", in_memory_mode)
        set("//tmp/t/@enable_lookup_hash_table", enable_lookup_hash_table)
        end_ts = generate_timestamp()

        sync_mount_table("//tmp/t")
        sleep(1.0)

        assert lookup_rows("//tmp/t", keys, timestamp=start_ts) == []
        actual = lookup_rows("//tmp/t", keys)
        assert actual == rows
        actual = lookup_rows("//tmp/t", keys, timestamp=end_ts)
        assert actual == rows
        actual = lookup_rows("//tmp/t", keys, keep_missing_rows=True)
        assert actual == rows + [None, None]
        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, rows)

        rows = [{"key": i, "avalue": 1} for i in xrange(2)]
        insert_rows("//tmp/t", rows, aggregate=True, update=True)

        expected = [{"key": i, "value": str(i), "avalue": 2} for i in xrange(2)]
        actual = lookup_rows("//tmp/t", keys)
        assert actual == expected
        actual = lookup_rows("//tmp/t", keys, keep_missing_rows=True)
        assert actual == expected + [None, None]
        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, expected)

        expected = [{"key": i, "avalue": 2} for i in xrange(2)]
        actual = lookup_rows("//tmp/t", keys, column_names=["key", "avalue"])
        assert actual == expected
        actual = lookup_rows("//tmp/t", keys, column_names=["key", "avalue"], keep_missing_rows=True)
        assert actual == expected + [None, None]
        actual = select_rows("key, avalue from [//tmp/t]")
        assert_items_equal(actual, expected)

        sync_unmount_table("//tmp/t")

        alter_table("//tmp/t", schema=[
                    {"name": "key", "type": "int64", "sort_order": "ascending"},
                    {"name": "key2", "type": "int64", "sort_order": "ascending"},
                    {"name": "nvalue", "type": "string"},
                    {"name": "value", "type": "string"},
                    {"name": "avalue", "type": "int64", "aggregate": "sum"}])

        sync_mount_table("//tmp/t")
        sleep(1.0)

        insert_rows("//tmp/t", rows, aggregate=True, update=True)

        expected = [{"key": i, "key2": None, "nvalue": None, "value": str(i), "avalue": 3} for i in xrange(2)]
        actual = lookup_rows("//tmp/t", keys)
        assert actual == expected
        actual = lookup_rows("//tmp/t", keys, keep_missing_rows=True)
        assert actual == expected + [None, None]
        actual = select_rows("* from [//tmp/t]")
        assert_items_equal(actual, expected)

        expected = [{"key": i, "avalue": 3} for i in xrange(2)]
        actual = lookup_rows("//tmp/t", keys, column_names=["key", "avalue"])
        assert actual == expected
        actual = lookup_rows("//tmp/t", keys, column_names=["key", "avalue"], keep_missing_rows=True)
        assert actual == expected + [None, None]
        actual = select_rows("key, avalue from [//tmp/t]")
        assert_items_equal(actual, expected)

    @authors("savrus")
    @parametrize_external
    def test_chunk_list_kind(self, external):
        sync_create_cells(1)
        self._create_simple_static_table("//tmp/t", external=external)
        write_table("//tmp/t", [{"key": 1, "value": "1"}])
        chunk_list = get("//tmp/t/@chunk_list_id")
        assert get("#{0}/@kind".format(chunk_list)) == "static"

        alter_table("//tmp/t", dynamic=True)
        root_chunk_list = get("//tmp/t/@chunk_list_id")
        tablet_chunk_list = get("#{0}/@child_ids/0".format(root_chunk_list))
        assert get("#{0}/@kind".format(root_chunk_list)) == "sorted_dynamic_root"
        assert get("#{0}/@kind".format(tablet_chunk_list)) == "sorted_dynamic_tablet"


    @authors("babenko")
    def test_no_commit_ordering(self):
        self._create_simple_table("//tmp/t")
        assert not exists("//tmp/t/@commit_ordering")


    @authors("babenko")
    def test_set_pivot_keys_upon_construction_fail(self):
        with pytest.raises(YtError):
            self._create_simple_table("//tmp/t", pivot_keys=[])
        with pytest.raises(YtError):
            self._create_simple_table("//tmp/t", pivot_keys=[[10], [20]])
        with pytest.raises(YtError):
            self._create_simple_table("//tmp/t", pivot_keys=[[], [1], [1]])

    @authors("babenko")
    def test_set_pivot_keys_upon_construction_success(self):
        self._create_simple_table("//tmp/t", pivot_keys=[[], [1], [2], [3]])
        assert get("//tmp/t/@tablet_count") == 4


    @authors("max42")
    def test_type_conversion(self):
        sync_create_cells(1)
        create("table", "//tmp/t",
            attributes={
                "dynamic": True,
                "schema": [
                    {"name": "int64", "type": "int64", "sort_order": "ascending"},
                    {"name": "uint64", "type": "uint64"},
                    {"name": "boolean", "type": "boolean"},
                    {"name": "double", "type": "double"},
                    {"name": "any", "type": "any"}]
            })
        sync_mount_table("//tmp/t")

        row1 = {
            "int64": yson.YsonUint64(3),
            "uint64": 42,
            "boolean": "false",
            "double": 18,
            "any": {}
        }
        row2 = {
            "int64": yson.YsonUint64(3)
        }

        yson_with_type_conversion = loads("<enable_type_conversion=%true>yson")
        yson_without_type_conversion = loads("<enable_integral_type_conversion=%false>yson")

        with pytest.raises(YtError):
            insert_rows("//tmp/t", [row1], input_format=yson_without_type_conversion)
        insert_rows("//tmp/t", [row1], input_format=yson_with_type_conversion)

        with pytest.raises(YtError):
            lookup_rows("//tmp/t", [row2], input_format=yson_without_type_conversion)
        assert len(lookup_rows("//tmp/t", [row2], input_format=yson_with_type_conversion)) == 1

        with pytest.raises(YtError):
            delete_rows("//tmp/t", [row2], input_format=yson_without_type_conversion)
        delete_rows("//tmp/t", [row2], input_format=yson_with_type_conversion)

        assert select_rows("* from [//tmp/t]") == []


    @authors("savrus")
    def test_retained_timestamp(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")

        t12 = get("//tmp/t/@retained_timestamp")
        t13 = get("//tmp/t/@unflushed_timestamp")
        assert t13 > t12

        t1 = generate_timestamp()
        assert t1 > t12
        # Wait for timestamp provider at the node.
        sleep(1)
        sync_mount_table("//tmp/t")
        # Wait for master to receive node statistics.
        sleep(1)
        t2 = get("//tmp/t/@unflushed_timestamp")
        assert t2 > t1
        assert get("//tmp/t/@retained_timestamp") == MinTimestamp

        rows = [{"key": i, "value": str(i)} for i in xrange(2)]
        insert_rows("//tmp/t", rows)
        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")
        t3 = get("//tmp/t/@retained_timestamp")
        t4 = get("//tmp/t/@unflushed_timestamp")
        assert t3 > MinTimestamp
        assert t2 < t4
        assert t3 < t4

        sleep(1)
        t11 = get("//tmp/t/@unflushed_timestamp")
        assert t4 < t11

        tx = start_transaction(timeout=60000)
        lock("//tmp/t", mode="snapshot", tx=tx)
        t5 = get("//tmp/t/@retained_timestamp", tx=tx)
        t6 = get("//tmp/t/@unflushed_timestamp", tx=tx)
        sleep(1)
        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")
        sleep(1)
        t7 = get("//tmp/t/@retained_timestamp")
        t8 = get("//tmp/t/@unflushed_timestamp")
        t9 = get("//tmp/t/@retained_timestamp", tx=tx)
        t10 = get("//tmp/t/@unflushed_timestamp", tx=tx)
        assert t5 == t9
        assert t6 == t10
        assert t5 < t7
        assert t6 < t8
        abort_transaction(tx)

        sync_freeze_table("//tmp/t")
        sleep(1)
        t14 = get("//tmp/t/@unflushed_timestamp")
        assert t14 > t8

    @authors("savrus", "levysotsky")
    def test_expired_timestamp(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@min_data_ttl", 0)

        ts = generate_timestamp()
        sleep(1)
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"key": 0}])
        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")
        sync_compact_table("//tmp/t")
        with pytest.raises(YtError):
            lookup_rows("//tmp/t", [{"key": 0}], timestamp=ts)

    @authors("savrus")
    def test_writer_config(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@chunk_writer", {"block_size": 1024})
        set("//tmp/t/@compression_codec", "none")
        sync_mount_table("//tmp/t")

        insert_rows("//tmp/t", [{"key": i, "value": "A"*1024} for i in xrange(10)])
        sync_unmount_table("//tmp/t")

        chunk_id = get_singular_chunk_id("//tmp/t")
        assert get("#" + chunk_id + "/@compressed_data_size") > 1024 * 10
        assert get("#" + chunk_id + "/@max_block_size") < 1024 * 2

    @authors("ifsmirnov", "savrus")
    def test_reshard_with_uncovered_chunk(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@min_data_ttl", 0)
        sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": str(i)} for i in xrange(3)]
        insert_rows("//tmp/t", rows)
        sync_unmount_table("//tmp/t")

        chunk_id = get_first_chunk_id("//tmp/t")
        sync_reshard_table("//tmp/t", [[], [1], [2]])

        def get_tablet_chunk_lists():
            return get("#{0}/@child_ids".format(get("//tmp/t/@chunk_list_id")))

        mount_table("//tmp/t", first_tablet_index=1, last_tablet_index=1)
        wait(lambda: get("//tmp/t/@tablets/1/state") == "mounted")
        delete_rows("//tmp/t", [{"key": 1}])
        sync_unmount_table("//tmp/t")

        set("//tmp/t/@forced_compaction_revision", 1)
        mount_table("//tmp/t", first_tablet_index=1, last_tablet_index=1)
        wait(lambda: get("//tmp/t/@tablets/1/state") == "mounted")
        tablet_chunk_lists = get_tablet_chunk_lists()
        wait(lambda: chunk_id not in get("#{0}/@child_ids".format(tablet_chunk_lists[1])))

        sync_unmount_table("//tmp/t")

        def get_chunk_under_chunk_view(chunk_view_id):
            return get("#{0}/@chunk_id".format(chunk_view_id))

        tablet_chunk_lists = get_tablet_chunk_lists()
        assert get_chunk_under_chunk_view(get("#{0}/@child_ids/0".format(tablet_chunk_lists[0]))) == chunk_id
        assert chunk_id not in get("#{0}/@child_ids".format(tablet_chunk_lists[1]))
        assert get_chunk_under_chunk_view(get("#{0}/@child_ids/0".format(tablet_chunk_lists[2]))) == chunk_id

        sync_reshard_table("//tmp/t", [[]])

        # Avoiding compaction.
        sync_mount_table("//tmp/t", freeze=True)
        assert list(lookup_rows("//tmp/t", [{"key": i} for i in xrange(3)])) == [
            {"key": i, "value": str(i)} for i in (0, 2)]

    @authors("ifsmirnov")
    def test_required_columns(self):
        schema = [
                {"name": "key_req", "type": "int64", "sort_order": "ascending", "required": True},
                {"name": "key_opt", "type": "int64", "sort_order": "ascending"},
                {"name": "value_req", "type": "string", "required": True},
                {"name": "value_opt", "type": "string"}]

        sync_create_cells(1)
        self._create_simple_table("//tmp/t", schema=schema)
        sync_mount_table("//tmp/t")

        with pytest.raises(YtError):
            insert_rows("//tmp/t", [dict()])
        with pytest.raises(YtError):
            insert_rows("//tmp/t", [dict(key_req=1, value_opt="data")])
        with pytest.raises(YtError):
            insert_rows("//tmp/t", [dict(key_opt=1, value_req="data", value_opt="data")])

        insert_rows("//tmp/t", [dict(key_req=1, value_req="data")])
        insert_rows("//tmp/t", [dict(key_req=1, key_opt=1, value_req="data", value_opt="data")])
        with pytest.raises(YtError):
            insert_rows("//tmp/t", [dict(key_req=1, key_opt=1, value_opt="other_data")], update=True)

        assert lookup_rows("//tmp/t", [dict(key_req=1, key_opt=1)]) == \
                [dict(key_req=1, key_opt=1, value_req="data", value_opt="data")]

        insert_rows("//tmp/t", [dict(key_req=1, key_opt=1, value_req="updated")], update=True)

        assert lookup_rows("//tmp/t", [dict(key_req=1, key_opt=1)]) == \
                [dict(key_req=1, key_opt=1, value_req="updated", value_opt="data")]

        with pytest.raises(YtError):
            delete_rows("//tmp/t", [dict(key_opt=1)])
        delete_rows("//tmp/t", [dict(key_req=1234)])
        delete_rows("//tmp/t", [dict(key_req=1, key_opt=1)])
        assert lookup_rows("//tmp/t", [dict(key_req=1, key_opt=1)]) == []

    @authors("ifsmirnov")
    def test_required_computed_columns(self):
        schema = [
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "computed", "type": "int64", "sort_order": "ascending", "expression": "key * 10", "required": True},
                {"name": "value", "type": "string"}]

        sync_create_cells(1)
        with pytest.raises(YtError):
            self._create_simple_table("//tmp/t", schema=schema)

    @authors("ifsmirnov")
    def test_required_aggregate_columns(self):
        schema = [
                {"name": "key", "type": "int64", "sort_order": "ascending"},
                {"name": "value", "type": "int64", "aggregate": "sum", "required": True}]

        sync_create_cells(1)
        self._create_simple_table("//tmp/t", schema=schema)
        sync_mount_table("//tmp/t")

        with pytest.raises(YtError):
            insert_rows("//tmp/t", [dict(key=1)])
        insert_rows("//tmp/t", [dict(key=1, value=2)])
        with pytest.raises(YtError):
            insert_rows("//tmp/t", [dict(key=1)])

    @authors("savrus")
    def test_partition_balancer(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        set("//tmp/t/@max_partition_data_size", 640)
        set("//tmp/t/@desired_partition_data_size", 512)
        set("//tmp/t/@min_partition_data_size", 256)
        set("//tmp/t/@compression_codec", "none")
        set("//tmp/t/@chunk_writer", {"block_size": 64})
        sync_mount_table("//tmp/t")

        tablet_id = get("//tmp/t/@tablets/0/tablet_id")
        address = get_tablet_leader_address(tablet_id)
        orchid = self._find_tablet_orchid(address, tablet_id)
        assert len(orchid["partitions"]) == 1

        insert_rows("//tmp/t", [{"key": i, "value": str(i)} for i in xrange(16)])
        sync_flush_table("//tmp/t")
        wait(lambda: len(self._find_tablet_orchid(address, tablet_id)["partitions"]) > 1)

    @authors("ifsmirnov")
    def test_partitioning_with_alter(self):
        # Creating two chunks with @eden=%false:
        # - [1]
        # - [1;1]
        # Three partitions should be created upon mount.
        sync_create_cells(1)
        schema = [
            {"name": "k1", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "int64"},
        ]
        self._create_simple_table("//tmp/t", schema=schema)

        # Create [1] chunk.
        sync_mount_table("//tmp/t")
        insert_rows("//tmp/t", [{"k1": 1}])
        sync_unmount_table("//tmp/t")
        set("//tmp/t/@forced_compaction_revision", 1)
        chunk_id = get("//tmp/t/@chunk_ids/0")
        sync_mount_table("//tmp/t")
        wait(lambda: get("//tmp/t/@chunk_ids/0") != chunk_id)
        assert not get("#{}/@eden".format(get("//tmp/t/@chunk_ids/0")))

        # Create [1;1] chunk.
        sync_unmount_table("//tmp/t")
        schema = schema[:1] + [{"name": "k2", "type": "int64", "sort_order": "ascending"}] + schema[1:]
        alter_table("//tmp/t", schema=schema)
        sync_reshard_table("//tmp/t", [[], [1,1]])
        sync_mount_table("//tmp/t", first_tablet_index=1, last_tablet_index=1)
        insert_rows("//tmp/t", [{"k1": 1, "k2": 1}])
        sync_unmount_table("//tmp/t")
        set("//tmp/t/@forced_compaction_revision", 1)
        chunk_id = get("//tmp/t/@chunk_ids/1")
        sync_mount_table("//tmp/t", first_tablet_index=1, last_tablet_index=1)
        wait(lambda: get("//tmp/t/@chunk_ids/1") != chunk_id)
        assert not get("#{}/@eden".format(get("//tmp/t/@chunk_ids/1")))

        sync_unmount_table("//tmp/t")
        set("//tmp/t/@enable_compaction_and_partitioning", False)
        sync_reshard_table("//tmp/t", [[]])
        sync_mount_table("//tmp/t")
        wait(lambda: get("//tmp/t/@tablet_statistics/partition_count") > 0)
        assert get("//tmp/t/@tablet_statistics/partition_count") == 3

        expected = [
            {"k1": 1L, "k2": yson.YsonEntity(), "value": yson.YsonEntity()},
            {"k1": 1L, "k2": 1L, "value": yson.YsonEntity()},
        ]

        assert list(lookup_rows("//tmp/t", [{"k1": 1}, {"k1": 1, "k2": 1}])) == expected
        assert list(select_rows("* from [//tmp/t] order by k1,k2 limit 100")) == expected

    @authors("ifsmirnov")
    def test_lookup_rich_ypath(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        sync_mount_table("//tmp/t")

        assert list(lookup_rows("//tmp/t", [{"key": 1}])) == []
        with pytest.raises(YtError):
            lookup_rows("//tmp/t[1:2]", [{"key": 1}])
        with pytest.raises(YtError):
            lookup_rows("//tmp/t{key}", [{"key": 1}])

    @authors("akozhikhov")
    def test_overlapping_store_count(self):
        # This magic combination of parameters makes flushed insertion to be stored in eden stores
        sync_create_cells(1)
        self._create_simple_table(
            "//tmp/t",
            max_partition_data_size=640,
            desired_partition_data_size=512,
            min_partition_data_size=256,
            compression_codec="none",
            chunk_writer={"block_size": 1},
        )
        sync_mount_table("//tmp/t")

        tablet_id = get("//tmp/t/@tablets/0/tablet_id")
        address = get_tablet_leader_address(tablet_id)

        def _check(stores, overlaps):
            orchid = self._find_tablet_orchid(address, tablet_id)
            assert stores == len(orchid["eden"]["stores"])
            wait(lambda: overlaps == get("//tmp/t/@tablet_statistics/overlapping_store_count"))
        _check(stores=1, overlaps=1)

        insert_rows("//tmp/t", [{"key": i, "value": str(i)} for i in xrange(12)])
        sync_flush_table("//tmp/t")
        _check(stores=2, overlaps=2)

        insert_rows("//tmp/t", [{"key": i, "value": str(i)} for i in xrange(12, 24)])
        sync_flush_table("//tmp/t")
        _check(stores=3, overlaps=2)

    @authors("savrus", "gridem")
    def test_expired_timestamp_read_remount(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", min_data_ttl=0, min_data_versions=0)

        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        ts = generate_timestamp()
        assert lookup_rows("//tmp/t", keys, timestamp=ts) == rows

        sync_flush_table("//tmp/t")
        sync_compact_table("//tmp/t")

        with pytest.raises(YtResponseError): lookup_rows("//tmp/t", keys, timestamp=ts)
        with pytest.raises(YtResponseError): select_rows("* from [//tmp/t]", timestamp=ts)

        remount_table("//tmp/t")

        with pytest.raises(YtResponseError): lookup_rows("//tmp/t", keys, timestamp=ts)
        with pytest.raises(YtResponseError): select_rows("* from [//tmp/t]", timestamp=ts)

        sync_unmount_table("//tmp/t")
        sync_mount_table("//tmp/t")

        with pytest.raises(YtResponseError): lookup_rows("//tmp/t", keys, timestamp=ts)
        with pytest.raises(YtResponseError): select_rows("* from [//tmp/t]", timestamp=ts)

    def test_expired_timestamp_read_flush(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t", min_data_ttl=0, min_data_versions=0, merge_rows_on_flush=True)

        sync_mount_table("//tmp/t")

        rows = [{"key": 1, "value": "2"}]
        keys = [{"key": 1}]
        insert_rows("//tmp/t", rows)

        ts = generate_timestamp()

        assert lookup_rows("//tmp/t", keys, timestamp=ts) == rows
        
        sync_flush_table("//tmp/t")

        with pytest.raises(YtResponseError): lookup_rows("//tmp/t", keys, timestamp=ts)
        with pytest.raises(YtResponseError): select_rows("* from [//tmp/t]", timestamp=ts)


##################################################################

class TestSortedDynamicTablesMemoryLimit(TestSortedDynamicTablesBase):
    NUM_NODES = 1
    DELTA_NODE_CONFIG = {
        "tablet_node": {
            "resource_limits": {
                "tablet_static_memory": 20000
            },
            "tablet_manager": {
                "preload_backoff_time": 5000
            },
        },
    }

    def _get_statistics(self, table):
        return get(table + "/@tablets/0/statistics")

    def _wait_preload(self, table):
        def is_preloaded():
            statistics = self._get_statistics(table)
            return (
                statistics["preload_completed_store_count"] > 0 and
                statistics["preload_pending_store_count"] == 0 and
                statistics["preload_failed_store_count"] == 0)

        wait(is_preloaded)

    def _wait_preload_failed(self, table):
        def is_preload_failed():
            statistics = self._get_statistics(table)
            return (
                statistics["preload_pending_store_count"] == 0 and
                statistics["preload_failed_store_count"] > 0)

        wait(is_preload_failed)

    @authors("savrus", "gridem")
    def test_in_memory_limit_exceeded(self):
        LARGE = "//tmp/large"
        SMALL = "//tmp/small"

        def table_create(table):
            self._create_simple_table(
                table,
                optimize_for="lookup",
                in_memory_mode="uncompressed",
                max_dynamic_store_row_count=10,
                replication_factor=1,
                read_quorum=1,
                write_quorum=1,
            )

            sync_mount_table(table)

        def check_lookup(table, keys, rows):
            assert lookup_rows(table, keys) == rows

        def generate_string(amount):
            return "x" * amount

        def table_insert_rows(length, table):
            rows = [{"key": i, "value": generate_string(length)} for i in xrange(10)]
            keys = [{"key": row["key"]} for row in rows]
            insert_rows(table, rows)
            return keys, rows

        tablet_cell_attributes = {
            "changelog_replication_factor": 1,
            "changelog_read_quorum": 1,
            "changelog_write_quorum": 1,
            "changelog_account": "sys",
            "snapshot_account": "sys"
        }

        set("//sys/tablet_cell_bundles/default/@options", tablet_cell_attributes)

        sync_create_cells(1)

        table_create(LARGE)
        table_create(SMALL)

        # create large table over memory limit
        large_data = table_insert_rows(10000, LARGE)
        sync_flush_table(LARGE)
        sync_unmount_table(LARGE)

        # create small table for final preload checking
        small_data = table_insert_rows(1000, SMALL)
        sync_flush_table(SMALL)
        sync_unmount_table(SMALL)

        # mount large table to trigger memory limit
        sync_mount_table(LARGE)
        self._wait_preload(LARGE)
        check_lookup(LARGE, *large_data)

        # mount small table, preload must fail
        sync_mount_table(SMALL)
        self._wait_preload_failed(SMALL)

        # unmounting large table releases the memory to allow small table to be preloaded
        sync_unmount_table(LARGE)
        self._wait_preload(SMALL)
        check_lookup(SMALL, *small_data)

        # cleanup
        sync_unmount_table(SMALL)

##################################################################

class TestSortedDynamicTablesMetadataCaching(TestSortedDynamicTablesBase):
    USE_MASTER_CACHE = True

    DELTA_DRIVER_CONFIG = {
        "max_rows_per_write_request": 2,

        "table_mount_cache": {
            "expire_after_successful_update_time": 60000,
            "refresh_time": 60000,
            "expire_after_failed_update_time": 1000,
            "expire_after_access_time": 300000
        }
    }

    # Reimplement dynamic table commands without calling clear_metadata_caches()

    def _mount_table(self, path, **kwargs):
        kwargs["path"] = path
        return execute_command("mount_table", kwargs)

    def _unmount_table(self, path, **kwargs):
        kwargs["path"] = path
        return execute_command("unmount_table", kwargs)

    def _reshard_table(self, path, arg, **kwargs):
        kwargs["path"] = path
        kwargs["pivot_keys"] = arg
        return execute_command("reshard_table", kwargs)

    def _sync_mount_table(self, path, **kwargs):
        self._mount_table(path, **kwargs)
        print_debug("Waiting for tablets to become mounted...")
        wait_for_tablet_state(path, "mounted", **kwargs)

    def _sync_unmount_table(self, path, **kwargs):
        self._unmount_table(path, **kwargs)
        print_debug("Waiting for tablets to become unmounted...")
        wait_for_tablet_state(path, "unmounted", **kwargs)


    @authors("savrus")
    def test_select_with_expired_schema(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        self._reshard_table("//tmp/t", [[], [1]])
        self._sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": str(i)} for i in xrange(2)]
        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        self._sync_unmount_table("//tmp/t")
        alter_table("//tmp/t", schema=[
                    {"name": "key", "type": "int64", "sort_order": "ascending"},
                    {"name": "key2", "type": "int64", "sort_order": "ascending"},
                    {"name": "value", "type": "string"}])
        self._sync_mount_table("//tmp/t")
        expected = [{"key": i, "key2": None, "value": str(i)} for i in xrange(2)]
        assert_items_equal(select_rows("* from [//tmp/t]"), expected)

    @authors("savrus")
    @skip_if_rpc_driver_backend
    def test_metadata_cache_invalidation(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t1", enable_compaction_and_partitioning=False)
        self._sync_mount_table("//tmp/t1")

        rows = [{"key": i, "value": str(i)} for i in xrange(3)]
        keys = [{"key": row["key"]} for row in rows]
        insert_rows("//tmp/t1", rows)
        assert_items_equal(lookup_rows("//tmp/t1", keys), rows)

        self._sync_unmount_table("//tmp/t1")
        with pytest.raises(YtError): lookup_rows("//tmp/t1", keys)
        clear_metadata_caches()
        self._sync_mount_table("//tmp/t1")

        assert_items_equal(lookup_rows("//tmp/t1", keys), rows)

        self._sync_unmount_table("//tmp/t1")
        with pytest.raises(YtError): select_rows("* from [//tmp/t1]")
        clear_metadata_caches()
        self._sync_mount_table("//tmp/t1")

        assert_items_equal(select_rows("* from [//tmp/t1]"), rows)

        def reshard_mounted_table(path, pivots):
            self._sync_unmount_table(path)
            self._reshard_table(path, pivots)
            self._sync_mount_table(path)

        reshard_mounted_table("//tmp/t1", [[], [1]])
        assert_items_equal(lookup_rows("//tmp/t1", keys), rows)

        reshard_mounted_table("//tmp/t1", [[], [1], [2]])
        assert_items_equal(select_rows("* from [//tmp/t1]"), rows)

        reshard_mounted_table("//tmp/t1", [[]])
        rows = [{"key": i, "value": str(i+1)} for i in xrange(3)]
        with pytest.raises(YtError): insert_rows("//tmp/t1", rows)
        insert_rows("//tmp/t1", rows)

        insert_rows("//tmp/t1", rows)
        assert_items_equal(lookup_rows("//tmp/t1", keys), rows)

    @authors("savrus")
    def test_lookup_from_removed_table(self):
        sync_create_cells(1)
        self._create_simple_table("//tmp/t")
        self._sync_mount_table("//tmp/t")
        rows = [{"key": i, "value": str(i)} for i in xrange(2)]

        # Do lookup to clear metadata and master cache.
        # Unfortunately master cache has old schema and it is retreived in driver where key is constructed.
        # Client invalidate&retry doesn't rebuild driver's key so this lookup has no chances to be completed.
        try:
            lookup_rows("//tmp/t", [{"key": 0}])
        except YtError:
            pass

        insert_rows("//tmp/t", rows)
        assert_items_equal(select_rows("* from [//tmp/t]"), rows)
        remove("//tmp/t")
        self._create_simple_table("//tmp/t")
        self._sync_mount_table("//tmp/t")
        actual = lookup_rows("//tmp/t", [{"key": 0}])
        assert actual == []

##################################################################

class TestSortedDynamicTablesMulticell(TestSortedDynamicTables):
    NUM_SECONDARY_MASTER_CELLS = 2

class TestSortedDynamicTablesMetadataCachingMulticell(TestSortedDynamicTablesMetadataCaching):
    NUM_SECONDARY_MASTER_CELLS = 2

##################################################################

class TestSortedDynamicTablesRpcProxy(TestSortedDynamicTables):
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True

class TestSortedDynamicTablesMetadataCachingRpcProxy(TestSortedDynamicTablesMetadataCaching):
    DRIVER_BACKEND = "rpc"
    ENABLE_RPC_PROXY = True

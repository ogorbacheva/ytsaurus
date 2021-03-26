from yt_env_setup import YTEnvSetup
from yt_commands import *

from time import sleep

#################################################################


class TestChunkMerger(YTEnvSetup):
    NUM_MASTERS = 5
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True
    ENABLE_BULK_INSERT = True

    DELTA_MASTER_CONFIG = {
        "chunk_manager": {
            "allow_multiple_erasure_parts_per_node": True,
            "chunk_merger": {
                "max_chunks_to_merge": 5
            }
        }
    }

    def _abort_chunk_merger_txs(self):
        txs = []
        for tx in ls("//sys/transactions", attributes=["title"]):
            title = tx.attributes.get("title", "")
            if "Chunk merger" in title:
                txs.append(tx)

        assert len(txs) > 0
        for tx in txs:
            abort_transaction(tx)

    @authors("aleksandra-zh")
    def test_merge_attributes(self):
        create("table", "//tmp/t")

        assert not get("//tmp/t/@enable_chunk_merger")
        set("//tmp/t/@enable_chunk_merger", True)
        assert get("//tmp/t/@enable_chunk_merger")

        create_account("a")
        assert get("//sys/accounts/a/@merge_job_rate_limit") == 0
        set("//sys/accounts/a/@merge_job_rate_limit", 7)
        assert get("//sys/accounts/a/@merge_job_rate_limit") == 7

    @authors("aleksandra-zh")
    def test_merge1(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")
        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"a": "c"})
        write_table("<append=true>//tmp/t", {"a": "d"})
        write_table("<append=true>//tmp/t", {"a": "e"})

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge2(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")
        write_table("//tmp/t", [
            {"a": 10},
            {"b": 50}
        ])
        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge_job_counter(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@merge_job_counter") > 0)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info
        wait(lambda: get("//tmp/t/@merge_job_counter") == 0)

    @authors("aleksandra-zh")
    def test_abort_merge_tx(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        info = read_table("//tmp/t")

        self._abort_chunk_merger_txs()

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        info = read_table("//tmp/t")
        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge_job_accounting(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create_account("a")
        create("table", "//tmp/t1", attributes={"account": "a"})

        write_table("<append=true>//tmp/t1", {"a": "b"})
        write_table("<append=true>//tmp/t1", {"b": "c"})
        write_table("<append=true>//tmp/t1", {"c": "d"})

        create_account("b")
        create("table", "//tmp/t2", attributes={"account": "b"})

        write_table("<append=true>//tmp/t2", {"a": "b"})
        write_table("<append=true>//tmp/t2", {"b": "c"})
        write_table("<append=true>//tmp/t2", {"c": "d"})

        set("//tmp/t1/@enable_chunk_merger", True)
        set("//tmp/t2/@enable_chunk_merger", True)

        set("//sys/accounts/b/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t2/@resource_usage/chunk_count") == 1)
        assert get("//tmp/t1/@resource_usage/chunk_count") > 1

        set("//sys/accounts/a/@merge_job_rate_limit", 10)
        wait(lambda: get("//tmp/t1/@resource_usage/chunk_count") == 1)

        self._abort_chunk_merger_txs()

    @authors("aleksandra-zh")
    def test_copy_merge(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")
        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"a": "c"})

        copy("//tmp/t", "//tmp/t1")
        info = read_table("//tmp/t")

        set("//tmp/t1/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t1/@resource_usage/chunk_count") == 1)
        assert get("//tmp/t/@resource_usage/chunk_count") > 1

        assert read_table("//tmp/t") == info
        assert read_table("//tmp/t1") == info

    @authors("aleksandra-zh")
    def test_schedule_again(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")
        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})
        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge_does_not_overwrite_data(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@merge_job_counter") > 0)

        write_table("//tmp/t", {"q": "r"})
        write_table("<append=true>//tmp/t", {"w": "t"})
        write_table("<append=true>//tmp/t", {"e": "y"})

        info = read_table("//tmp/t")

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)

        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_remove(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@merge_job_counter") > 0)

        remove("//tmp/t")

        # Just hope nothing crashes.
        sleep(5)

    @authors("aleksandra-zh")
    def test_schema(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create(
            "table",
            "//tmp/t",
            attributes={
                "schema": make_schema(
                    [
                        {"name": "key", "type": "int64"},
                        {"name": "value", "type": "string"},
                    ]
                ),
            },
        )

        write_table("<append=true>//tmp/t", {"key": 1, "value": "a"})
        write_table("<append=true>//tmp/t", {"key": 2, "value": "b"})
        write_table("<append=true>//tmp/t", {"key": 3, "value": "c"})

        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)

        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_sorted(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create(
            "table",
            "//tmp/t",
            attributes={
                "schema": make_schema(
                    [
                        {"name": "key", "type": "int64", "sort_order": "ascending"},
                        {"name": "value", "type": "string"},
                    ]
                ),
            },
        )

        write_table("<append=true>//tmp/t", {"key": 1, "value": "a"})
        write_table("<append=true>//tmp/t", {"key": 2, "value": "b"})
        write_table("<append=true>//tmp/t", {"key": 3, "value": "c"})

        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)

        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge_merge(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t1")
        write_table("<append=true>//tmp/t1", {"a": "b"})
        write_table("<append=true>//tmp/t1", {"b": "c"})

        create("table", "//tmp/t2")
        write_table("<append=true>//tmp/t2", {"key": 1, "value": "a"})
        write_table("<append=true>//tmp/t2", {"key": 2, "value": "b"})

        create("table", "//tmp/t")
        merge(mode="unordered", in_=["//tmp/t1", "//tmp/t2"], out="//tmp/t")

        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)

        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge_chunks_exceed_max_chunk_to_merge_limit(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        create("table", "//tmp/t")
        for i in range(10):
            write_table("<append=true>//tmp/t", {"a": "b"})

        assert get("//tmp/t/@resource_usage/chunk_count") == 10
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") <= 2)
        assert read_table("//tmp/t") == info

        # Initiate another merge.
        write_table("<append=true>//tmp/t", {"a": "b"})
        info = read_table("//tmp/t")

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

    @authors("aleksandra-zh")
    def test_merge_job_rate_limit_permission(self):
        create_account("a")
        create_user("u")
        acl = [
            make_ace("allow", "u", ["use", "modify_children"], "object_only"),
            make_ace("allow", "u", ["write", "remove", "administer"], "descendants_only"),
        ]
        set("//sys/account_tree/a/@acl", acl)

        create_account("b", "a", authenticated_user="u")

        with pytest.raises(YtError):
            set("//sys/accounts/a/@merge_job_rate_limit", 10, authenticated_user="u")

        with pytest.raises(YtError):
            set("//sys/accounts/b/@merge_job_rate_limit", 10, authenticated_user="u")

        create("table", "//tmp/t")
        with pytest.raises(YtError):
            set("//tmp/t/@enable_chunk_merger", True, authenticated_user="u")

    @authors("aleksandra-zh")
    def test_do_not_crash_on_dynamic_table(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        sync_create_cells(1)
        create("table", "//tmp/t1")

        schema = [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "value", "type": "string"},
        ]

        create_dynamic_table("//tmp/t2", schema=schema)
        sync_mount_table("//tmp/t2")

        insert_rows("//tmp/t2", [{"key": 1, "value": "1"}])
        insert_rows("//tmp/t2", [{"key": 2, "value": "1"}])

        sync_unmount_table("//tmp/t2")
        sync_mount_table("//tmp/t2")

        write_table("<append=true>//tmp/t1", {"key": 3, "value": "1"})

        op = map(in_="//tmp/t1", out="<append=true>//tmp/t2", command="cat")

        set("//tmp/t2/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        # Just do not crash, please.
        sleep(10)

    @authors("aleksandra-zh")
    def test_compression_codec(self):
        codec = "lz4"
        create("table", "//tmp/t", attributes={"compression_codec": codec})

        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        chunk_ids = get("//tmp/t/@chunk_ids")
        assert get("#{0}/@compression_codec".format(chunk_ids[0])) == codec

        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

        chunk_ids = get("//tmp/t/@chunk_ids")
        assert get("#{0}/@compression_codec".format(chunk_ids[0])) == codec

    @authors("aleksandra-zh")
    def test_change_compression_codec(self):
        codec1 = "lz4"
        codec2 = "zstd_17"
        create("table", "//tmp/t", attributes={"compression_codec": codec1})

        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        chunk_ids = get("//tmp/t/@chunk_ids")
        assert get("#{0}/@compression_codec".format(chunk_ids[0])) == codec1

        info = read_table("//tmp/t")

        set("//tmp/t/@compression_codec", codec2)

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

        chunk_ids = get("//tmp/t/@chunk_ids")
        assert get("#{0}/@compression_codec".format(chunk_ids[0])) == codec2

    @authors("aleksandra-zh")
    def test_erasure(self):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)

        codec = "lrc_12_2_2"
        create("table", "//tmp/t", attributes={"erasure_codec": codec})

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"b": "c"})
        write_table("<append=true>//tmp/t", {"c": "d"})

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        sleep(5)

        assert get("//tmp/t/@merge_job_counter") == 0

    @authors("aleksandra-zh")
    @pytest.mark.parametrize("optimize_for", ["scan", "lookup"])
    def test_optimize_for(self, optimize_for):
        set("//sys/@config/chunk_manager/chunk_merger/enable", True)
        create("table", "//tmp/t", attributes={"optimize_for": optimize_for})

        write_table("<append=true>//tmp/t", {"a": "b"})
        write_table("<append=true>//tmp/t", {"a": "c"})
        write_table("<append=true>//tmp/t", {"a": "d"})

        assert get("//tmp/t/@resource_usage/chunk_count") > 1
        info = read_table("//tmp/t")

        set("//tmp/t/@enable_chunk_merger", True)
        set("//sys/accounts/tmp/@merge_job_rate_limit", 10)

        wait(lambda: get("//tmp/t/@resource_usage/chunk_count") == 1)
        assert read_table("//tmp/t") == info

        chunk_format = "schemaless_horizontal" if optimize_for == "lookup" else "unversioned_columnar"
        chunk_ids = get("//tmp/t/@chunk_ids")
        assert get("#{0}/@table_chunk_format".format(chunk_ids[0])) == chunk_format


class TestChunkMergerMulticell(TestChunkMerger):
    NUM_SECONDARY_MASTER_CELLS = 2


class TestChunkMergerPortal(TestChunkMergerMulticell):
    ENABLE_TMP_PORTAL = True
    NUM_SECONDARY_MASTER_CELLS = 3

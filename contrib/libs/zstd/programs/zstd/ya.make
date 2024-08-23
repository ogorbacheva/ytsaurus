# Generated by devtools/yamaker.

PROGRAM()

WITHOUT_LICENSE_TEXTS()

VERSION(1.5.6)

PEERDIR(
    contrib/libs/zstd
)

ADDINCL(
    contrib/libs/zstd/lib
    contrib/libs/zstd/lib/common
    contrib/libs/zstd/programs
)

NO_COMPILER_WARNINGS()

NO_RUNTIME()

CFLAGS(
    -DZSTD_LEGACY_SUPPORT=1
    -DZSTD_MULTITHREAD
)

SRCDIR(contrib/libs/zstd/programs)

SRCS(
    benchfn.c
    benchzstd.c
    datagen.c
    dibio.c
    fileio.c
    fileio_asyncio.c
    lorem.c
    timefn.c
    util.c
    zstdcli.c
    zstdcli_trace.c
)

END()

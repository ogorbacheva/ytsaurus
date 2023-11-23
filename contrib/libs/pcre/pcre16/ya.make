# Generated by devtools/yamaker.

LIBRARY()

WITHOUT_LICENSE_TEXTS()

LICENSE(BSD-3-Clause)

ADDINCL(
    contrib/libs/pcre
)

NO_COMPILER_WARNINGS()

NO_RUNTIME()

CFLAGS(
    -DHAVE_CONFIG_H
)

IF(PCRE_LINK_SIZE)
    CFLAGS(
        -DPCRE_LINK_SIZE=$PCRE_LINK_SIZE
    )
ENDIF()

SRCDIR(contrib/libs/pcre)

SRCS(
    pcre16_byte_order.c
    pcre16_chartables.c
    pcre16_compile.c
    pcre16_config.c
    pcre16_dfa_exec.c
    pcre16_exec.c
    pcre16_fullinfo.c
    pcre16_get.c
    pcre16_globals.c
    pcre16_jit_compile.c
    pcre16_maketables.c
    pcre16_newline.c
    pcre16_ord2utf16.c
    pcre16_refcount.c
    pcre16_string_utils.c
    pcre16_study.c
    pcre16_tables.c
    pcre16_ucd.c
    pcre16_utf16_utils.c
    pcre16_valid_utf16.c
    pcre16_version.c
    pcre16_xclass.c
    pcre_chartables.c
)

END()

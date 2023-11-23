# Generated by devtools/yamaker.

LIBRARY()

WITHOUT_LICENSE_TEXTS()

LICENSE(BSD-3-Clause)

PEERDIR(
    contrib/libs/pcre
)

ADDINCL(
    contrib/libs/pcre
)

NO_COMPILER_WARNINGS()

NO_UTIL()

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
    pcre_scanner.cc
    pcre_stringpiece.cc
    pcrecpp.cc
)

END()

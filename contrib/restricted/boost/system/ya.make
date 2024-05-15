# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(BSL-1.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.85.0)

ORIGINAL_SOURCE(https://github.com/boostorg/system/archive/boost-1.85.0.tar.gz)

PEERDIR(
    contrib/restricted/boost/assert
    contrib/restricted/boost/config
    contrib/restricted/boost/throw_exception
    contrib/restricted/boost/variant2
    contrib/restricted/boost/winapi
)

ADDINCL(
    GLOBAL contrib/restricted/boost/system/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

END()

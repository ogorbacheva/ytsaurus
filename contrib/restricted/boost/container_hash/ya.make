# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(BSL-1.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.85.0)

ORIGINAL_SOURCE(https://github.com/boostorg/container_hash/archive/boost-1.85.0.tar.gz)

PEERDIR(
    contrib/restricted/boost/config
    contrib/restricted/boost/describe
    contrib/restricted/boost/mp11
)

ADDINCL(
    GLOBAL contrib/restricted/boost/container_hash/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

END()

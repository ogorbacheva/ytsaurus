# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(BSL-1.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.85.0)

ORIGINAL_SOURCE(https://github.com/boostorg/range/archive/boost-1.85.0.tar.gz)

PEERDIR(
    contrib/restricted/boost/array
    contrib/restricted/boost/assert
    contrib/restricted/boost/concept_check
    contrib/restricted/boost/config
    contrib/restricted/boost/container_hash
    contrib/restricted/boost/conversion
    contrib/restricted/boost/core
    contrib/restricted/boost/detail
    contrib/restricted/boost/iterator
    contrib/restricted/boost/mpl
    contrib/restricted/boost/optional
    contrib/restricted/boost/preprocessor
    contrib/restricted/boost/regex
    contrib/restricted/boost/static_assert
    contrib/restricted/boost/tuple
    contrib/restricted/boost/type_traits
    contrib/restricted/boost/utility
)

ADDINCL(
    GLOBAL contrib/restricted/boost/range/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

END()

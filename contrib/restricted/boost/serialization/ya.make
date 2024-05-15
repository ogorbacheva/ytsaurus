# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(
    BSL-1.0 AND
    Zlib
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.85.0)

ORIGINAL_SOURCE(https://github.com/boostorg/serialization/archive/boost-1.85.0.tar.gz)

PEERDIR(
    contrib/restricted/boost/array
    contrib/restricted/boost/assert
    contrib/restricted/boost/config
    contrib/restricted/boost/core
    contrib/restricted/boost/detail
    contrib/restricted/boost/function
    contrib/restricted/boost/integer
    contrib/restricted/boost/io
    contrib/restricted/boost/iterator
    contrib/restricted/boost/move
    contrib/restricted/boost/mpl
    contrib/restricted/boost/optional
    contrib/restricted/boost/predef
    contrib/restricted/boost/preprocessor
    contrib/restricted/boost/smart_ptr
    contrib/restricted/boost/spirit
    contrib/restricted/boost/static_assert
    contrib/restricted/boost/type_traits
    contrib/restricted/boost/unordered
    contrib/restricted/boost/utility
    contrib/restricted/boost/variant
)

ADDINCL(
    GLOBAL contrib/restricted/boost/serialization/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

IF (DYNAMIC_BOOST)
    CFLAGS(
        GLOBAL -DBOOST_SERIALIZATION_DYN_LINK
    )
ENDIF()

SRCS(
    src/archive_exception.cpp
    src/basic_archive.cpp
    src/basic_iarchive.cpp
    src/basic_iserializer.cpp
    src/basic_oarchive.cpp
    src/basic_oserializer.cpp
    src/basic_pointer_iserializer.cpp
    src/basic_pointer_oserializer.cpp
    src/basic_serializer_map.cpp
    src/basic_text_iprimitive.cpp
    src/basic_text_oprimitive.cpp
    src/basic_text_wiprimitive.cpp
    src/basic_text_woprimitive.cpp
    src/basic_xml_archive.cpp
    src/binary_iarchive.cpp
    src/binary_oarchive.cpp
    src/binary_wiarchive.cpp
    src/binary_woarchive.cpp
    src/codecvt_null.cpp
    src/extended_type_info.cpp
    src/extended_type_info_no_rtti.cpp
    src/extended_type_info_typeid.cpp
    src/polymorphic_binary_iarchive.cpp
    src/polymorphic_binary_oarchive.cpp
    src/polymorphic_iarchive.cpp
    src/polymorphic_oarchive.cpp
    src/polymorphic_text_iarchive.cpp
    src/polymorphic_text_oarchive.cpp
    src/polymorphic_text_wiarchive.cpp
    src/polymorphic_text_woarchive.cpp
    src/polymorphic_xml_iarchive.cpp
    src/polymorphic_xml_oarchive.cpp
    src/polymorphic_xml_wiarchive.cpp
    src/polymorphic_xml_woarchive.cpp
    src/stl_port.cpp
    src/text_iarchive.cpp
    src/text_oarchive.cpp
    src/text_wiarchive.cpp
    src/text_woarchive.cpp
    src/utf8_codecvt_facet.cpp
    src/void_cast.cpp
    src/xml_archive_exception.cpp
    src/xml_grammar.cpp
    src/xml_iarchive.cpp
    src/xml_oarchive.cpp
    src/xml_wgrammar.cpp
    src/xml_wiarchive.cpp
    src/xml_woarchive.cpp
)

END()

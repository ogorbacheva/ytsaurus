# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(
    BSL-1.0 AND
    MIT AND
    NCSA
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.85.0)

ORIGINAL_SOURCE(https://github.com/boostorg/thread/archive/boost-1.85.0.tar.gz)

PEERDIR(
    contrib/restricted/boost/assert
    contrib/restricted/boost/atomic
    contrib/restricted/boost/bind
    contrib/restricted/boost/chrono
    contrib/restricted/boost/concept_check
    contrib/restricted/boost/config
    contrib/restricted/boost/container
    contrib/restricted/boost/container_hash
    contrib/restricted/boost/core
    contrib/restricted/boost/date_time
    contrib/restricted/boost/exception
    contrib/restricted/boost/function
    contrib/restricted/boost/io
    contrib/restricted/boost/move
    contrib/restricted/boost/optional
    contrib/restricted/boost/predef
    contrib/restricted/boost/preprocessor
    contrib/restricted/boost/smart_ptr
    contrib/restricted/boost/static_assert
    contrib/restricted/boost/system
    contrib/restricted/boost/throw_exception
    contrib/restricted/boost/tuple
    contrib/restricted/boost/type_traits
    contrib/restricted/boost/utility
    contrib/restricted/boost/winapi
)

ADDINCL(
    GLOBAL contrib/restricted/boost/thread/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

IF (DYNAMIC_BOOST)
    CFLAGS(
        -DBOOST_THREAD_BUILD_DLL
        GLOBAL -DBOOST_THREAD_USE_DLL
    )
ELSE()
    CFLAGS(
        -DBOOST_THREAD_BUILD_LIB
        GLOBAL -DBOOST_THREAD_USE_LIB
    )
ENDIF()

SRCS(
    src/future.cpp
)

IF (OS_WINDOWS)
    CFLAGS(
        GLOBAL -DBOOST_THREAD_WIN32
        -DBOOST_THREAD_USES_CHRONO
        -DWIN32_LEAN_AND_MEAN
        -DBOOST_USE_WINDOWS_H
    )
    SRCS(
        src/win32/thread.cpp
        src/win32/thread_primitives.cpp
        src/win32/tss_dll.cpp
        src/win32/tss_pe.cpp
    )
ELSE()
    CFLAGS(
        GLOBAL -DBOOST_THREAD_POSIX
        -DBOOST_THREAD_DONT_USE_CHRONO
    )
    SRCS(
        src/pthread/once.cpp
        src/pthread/thread.cpp
    )
ENDIF()

END()

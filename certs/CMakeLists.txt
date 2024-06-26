# This file was generated by the build system used internally in Yandex and called "ya"
# (https://github.com/yandex/yatool).
#
# Configuration files of ya build system are usually named ya.make. If ya.make file is present
# at the root of the repository, then this repository supports ya build.
#
# If the repository supports both CMake and ya build configurations, please modify both of them.
#
# If only CMake build configuration is supported then modify only CMake files and note that only
# simple modifications are allowed like adding source-files to targets or adding simple properties
# like target_include_directories. These modifications will be ported to original ya.make files
# by maintainers. Any complex modifications which can't be easily ported back to the ya build
# system may be rejected.

find_package(linux-headers-generic REQUIRED)

get_built_tool_path(
  TOOL_rescompiler_bin
  TOOL_rescompiler_dependency
  tools/rescompiler/bin
  rescompiler
)

add_library(certs INTERFACE)

target_link_libraries(certs INTERFACE
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
)


add_global_library_for(certs.global
  certs
)

target_link_libraries(certs.global PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
)

target_sources(certs.global PRIVATE
  ${PROJECT_BINARY_DIR}/certs/b03c08f91a00a60ac9d7daef45f3075a.cpp
)

resources(certs.global
  ${PROJECT_BINARY_DIR}/certs/b03c08f91a00a60ac9d7daef45f3075a.cpp
  INPUTS
  ${PROJECT_SOURCE_DIR}/certs/cacert.pem
  KEYS
  /builtin/cacert
)


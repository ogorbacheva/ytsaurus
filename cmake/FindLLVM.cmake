#  Find LLVM includes and libraries.
#
#  LLVM_VERSION      - LLVM version.
#  LLVM_INCLUDE_DIRS - Directory containing LLVM headers.
#  LLVM_LIBRARY_DIRS - Directory containing LLVM libraries.
#  LLVM_CPPFLAGS     - C preprocessor flags for files that include LLVM headers.
#  LLVM_CXXFLAGS     - C++ compiler flags for files that include LLVM headers.
#  LLVM_LDFLAGS      - Linker flags.
#  LLVM_FOUND        - True if LLVM was found.

#  llvm_map_components_to_libraries - Maps LLVM used components to required libraries.
#  Usage: llvm_map_components_to_libraries(REQUIRED_LLVM_LIBRARIES core jit interpreter native ...)

find_program(LLVM_CONFIG_EXECUTABLE
  NAMES llvm-config-3.7 llvm-config-mp-3.7 llvm-config
  PATHS $ENV{LLVM_ROOT}/bin
)

mark_as_advanced(LLVM_CONFIG_EXECUTABLE)

if(NOT LLVM_CONFIG_EXECUTABLE)
  message(FATAL_ERROR "Cannot find LLVM (looking for `llvm-config`). Please, provide LLVM_ROOT environment variable.")
else()
  set(LLVM_FOUND TRUE)

  find_program(CLANG_EXECUTABLE
    NAMES clang-3.7 clang-mp-3.7 clang
    PATHS $ENV{LLVM_ROOT}/bin
  )

  find_program(CLANGPP_EXECUTABLE
    NAMES clang++-3.7 clang++-mp-3.7 clang++
    PATHS $ENV{LLVM_ROOT}/bin
  )

  find_program(LLVM_LINK_EXECUTABLE
    NAMES llvm-link-3.7 llvm-link-mp-3.7 llvm-link
    PATHS $ENV{LLVM_ROOT}/bin
  )

  find_program(LLVM_OPT_EXECUTABLE
    NAMES opt-3.7 opt-mp-3.7 opt
    PATHS $ENV{LLVM_ROOT}/bin
  )

  execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
    OUTPUT_VARIABLE LLVM_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(LLVM_VERSION VERSION_LESS "3.7")
    message(FATAL_ERROR "LLVM 3.7+ is required.")
  endif()

  execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
    OUTPUT_VARIABLE LLVM_INCLUDE_DIRS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
    OUTPUT_VARIABLE LLVM_LIBRARY_DIRS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --cppflags
    OUTPUT_VARIABLE LLVM_CPPFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --cxxflags
    OUTPUT_VARIABLE LLVM_CXXFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
    OUTPUT_VARIABLE LLVM_LDFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  # Get the link libs we need.
  function(llvm_map_components_to_libraries RESULT)
    if(MSVC OR MSVC_IDE)
      # Workaround
      execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs ${ARGN}
        OUTPUT_VARIABLE _tmp
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )

      string(REPLACE "-l" " " _tmp "${_tmp}")
      string(REPLACE "  " " " _tmp "${_tmp}")
      string(REPLACE " "  ";" _tmp "${_tmp}")

      set(_libs_module "")
      foreach(_tmp_item ${_tmp})
        list(APPEND _libs_module "${LLVM_LIBRARY_DIRS}/${_tmp_item}.lib")
      endforeach()
    else()
      execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs ${ARGN}
        OUTPUT_VARIABLE _tmp
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )

      string(REPLACE " " ";" _libs_module "${_tmp}")
    endif()

    message(STATUS "LLVM Libraries for '${ARGN}': ${_libs_module}")

    execute_process(
      COMMAND ${LLVM_CONFIG_EXECUTABLE} --system-libs ${ARGN}
      OUTPUT_VARIABLE _libs_system
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    string(REPLACE "\n" " " _libs_system "${_libs_system}")
    string(REPLACE "  " " " _libs_system "${_libs_system}")
    string(REPLACE " "  ";" _libs_system "${_libs_system}")

    set(${RESULT} ${_libs_module} ${_libs_system} PARENT_SCOPE)
  endfunction(llvm_map_components_to_libraries)

  message(STATUS "LLVM Include Directory: ${LLVM_INCLUDE_DIRS}")
  message(STATUS "LLVM Library Directory: ${LLVM_LIBRARY_DIRS}")
  message(STATUS "LLVM C++ Preprocessor: ${LLVM_CPPFLAGS}")
  message(STATUS "LLVM C++ Compiler: ${LLVM_CXXFLAGS}")
endif()




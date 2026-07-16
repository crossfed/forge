if(NOT DEFINED FORGE_PACKAGE_TEST_SOURCE_DIR)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_SOURCE_DIR is required")
endif()
if(NOT DEFINED FORGE_PACKAGE_TEST_BINARY_DIR)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_BINARY_DIR is required")
endif()
if(NOT DEFINED FORGE_PACKAGE_TEST_PREFIX)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_PREFIX is required")
endif()
if(NOT DEFINED FORGE_PACKAGE_TEST_GENERATOR)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_GENERATOR is required")
endif()

set(_forge_success_binary_dir "${FORGE_PACKAGE_TEST_BINARY_DIR}-success")
file(REMOVE_RECURSE "${FORGE_PACKAGE_TEST_BINARY_DIR}" "${_forge_success_binary_dir}")

set(
   _forge_common_configure_options
   -DCMAKE_PREFIX_PATH=${FORGE_PACKAGE_TEST_PREFIX}
   -DCMAKE_CXX_COMPILER=${FORGE_PACKAGE_TEST_CXX_COMPILER}
   -DCMAKE_C_COMPILER=${FORGE_PACKAGE_TEST_C_COMPILER}
)

if(FORGE_PACKAGE_TEST_OSX_SYSROOT)
   list(APPEND _forge_common_configure_options -DCMAKE_OSX_SYSROOT=${FORGE_PACKAGE_TEST_OSX_SYSROOT})
endif()
if(FORGE_PACKAGE_TEST_CLANG_DIR)
   list(APPEND _forge_common_configure_options -DClang_DIR=${FORGE_PACKAGE_TEST_CLANG_DIR})
endif()
if(FORGE_PACKAGE_TEST_LLVM_DIR)
   list(APPEND _forge_common_configure_options -DLLVM_DIR=${FORGE_PACKAGE_TEST_LLVM_DIR})
endif()

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -S "${FORGE_PACKAGE_TEST_SOURCE_DIR}"
      -B "${_forge_success_binary_dir}"
      -G "${FORGE_PACKAGE_TEST_GENERATOR}"
      ${_forge_common_configure_options}
   RESULT_VARIABLE _forge_success_result
   OUTPUT_VARIABLE _forge_success_stdout
   ERROR_VARIABLE _forge_success_stderr
)

if(NOT _forge_success_result EQUAL 0)
   message(FATAL_ERROR "find_package(Forge COMPONENTS all) rejected the installed component set:\n${_forge_success_stdout}\n${_forge_success_stderr}")
endif()

if(FORGE_PACKAGE_TEST_BUILT_WITH_TUI)
   set(
      _forge_failure_configure_options
      ${_forge_common_configure_options}
      -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON
   )

   execute_process(
      COMMAND
         "${CMAKE_COMMAND}"
         -S "${FORGE_PACKAGE_TEST_SOURCE_DIR}"
         -B "${FORGE_PACKAGE_TEST_BINARY_DIR}"
         -G "${FORGE_PACKAGE_TEST_GENERATOR}"
         ${_forge_failure_configure_options}
      RESULT_VARIABLE _forge_configure_result
      OUTPUT_VARIABLE _forge_configure_stdout
      ERROR_VARIABLE _forge_configure_stderr
   )

   string(CONCAT _forge_configure_log "${_forge_configure_stdout}\n${_forge_configure_stderr}")

   if(_forge_configure_result EQUAL 0)
      message(FATAL_ERROR "Expected find_package(Forge COMPONENTS all) to fail when notcurses discovery is disabled")
   endif()

   if(NOT _forge_configure_log MATCHES "Forge tui component requires notcurses-core|Forge.*all|Could NOT find Forge")
      message(FATAL_ERROR "Unexpected configure failure:\n${_forge_configure_log}")
   endif()

   message(STATUS "find_package(Forge COMPONENTS all) failed as expected when notcurses discovery is disabled")
else()
   message(STATUS "Skipping the missing Notcurses check because the installed Forge package was built without TUI")
endif()

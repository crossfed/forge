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

set(
   _forge_configure_options
   -DCMAKE_PREFIX_PATH=${FORGE_PACKAGE_TEST_PREFIX}
   -DCMAKE_CXX_COMPILER=${FORGE_PACKAGE_TEST_CXX_COMPILER}
   -DCMAKE_C_COMPILER=${FORGE_PACKAGE_TEST_C_COMPILER}
)

if(FORGE_PACKAGE_TEST_OSX_SYSROOT)
   list(APPEND _forge_configure_options -DCMAKE_OSX_SYSROOT=${FORGE_PACKAGE_TEST_OSX_SYSROOT})
endif()
if(FORGE_PACKAGE_TEST_CLANG_DIR)
   list(APPEND _forge_configure_options -DClang_DIR=${FORGE_PACKAGE_TEST_CLANG_DIR})
endif()
if(FORGE_PACKAGE_TEST_LLVM_DIR)
   list(APPEND _forge_configure_options -DLLVM_DIR=${FORGE_PACKAGE_TEST_LLVM_DIR})
endif()

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -S "${FORGE_PACKAGE_TEST_SOURCE_DIR}"
      -B "${FORGE_PACKAGE_TEST_BINARY_DIR}"
      -G "${FORGE_PACKAGE_TEST_GENERATOR}"
      ${_forge_configure_options}
   RESULT_VARIABLE _forge_configure_result
   OUTPUT_VARIABLE _forge_configure_stdout
   ERROR_VARIABLE _forge_configure_stderr
)

if(_forge_configure_result EQUAL 0)
   message(FATAL_ERROR "Expected plugins_crypto_signer to be an unknown Forge package component")
endif()

string(CONCAT _forge_configure_log "${_forge_configure_stdout}\n${_forge_configure_stderr}")
if(NOT _forge_configure_log MATCHES "Unknown FORGE component: plugins_crypto_signer")
   message(FATAL_ERROR "Unexpected configure failure:\n${_forge_configure_log}")
endif()

message(STATUS "plugins_crypto_signer is rejected as an unknown Forge package component")

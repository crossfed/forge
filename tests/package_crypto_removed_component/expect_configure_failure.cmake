if(NOT DEFINED FORGE_PACKAGE_TEST_BINARY_DIR)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_BINARY_DIR is required")
endif()
if(NOT DEFINED FORGE_PACKAGE_TEST_PREFIX)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_PREFIX is required")
endif()
if(NOT DEFINED FORGE_PACKAGE_TEST_GENERATOR)
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_GENERATOR is required")
endif()

function(_forge_expect_removed_crypto_failure case_name find_arguments)
   set(_source_dir "${FORGE_PACKAGE_TEST_BINARY_DIR}/${case_name}-source")
   set(_binary_dir "${FORGE_PACKAGE_TEST_BINARY_DIR}/${case_name}-build")
   file(REMOVE_RECURSE "${_source_dir}" "${_binary_dir}")
   file(MAKE_DIRECTORY "${_source_dir}")
   file(
      WRITE "${_source_dir}/CMakeLists.txt"
      "cmake_minimum_required(VERSION 3.31)\n"
      "project(forge-removed-crypto-component LANGUAGES NONE)\n"
      "find_package(Forge ${find_arguments})\n"
   )

   execute_process(
      COMMAND
         "${CMAKE_COMMAND}"
         -S "${_source_dir}"
         -B "${_binary_dir}"
         -G "${FORGE_PACKAGE_TEST_GENERATOR}"
         -DCMAKE_PREFIX_PATH=${FORGE_PACKAGE_TEST_PREFIX}
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _stdout
      ERROR_VARIABLE _stderr
   )
   string(CONCAT _log "${_stdout}\n${_stderr}")

   if(_result EQUAL 0)
      message(FATAL_ERROR "${case_name}: removed crypto component was accepted")
   endif()
   if(NOT _log MATCHES "Forge crypto component was removed")
      message(FATAL_ERROR "${case_name}: unexpected configure failure:\n${_log}")
   endif()
endfunction()

_forge_expect_removed_crypto_failure(optional "CONFIG OPTIONAL_COMPONENTS crypto")
_forge_expect_removed_crypto_failure(required_optional "CONFIG REQUIRED OPTIONAL_COMPONENTS crypto")
_forge_expect_removed_crypto_failure(shorthand "CONFIG REQUIRED crypto")
_forge_expect_removed_crypto_failure(semicolon [=[CONFIG REQUIRED COMPONENTS "raw;crypto"]=])

message(STATUS "Removed Forge crypto component is rejected in every supported request form")

foreach(_required IN ITEMS
   FORGE_SOURCE_DIR
   FORGE_TEST_BINARY_DIR
   FORGE_TEST_GENERATOR
   FORGE_TEST_C_COMPILER
   FORGE_TEST_CXX_COMPILER
)
   if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
      message(FATAL_ERROR "Missing required test input: ${_required}")
   endif()
endforeach()

set(
   _configure_arguments
   -S "${FORGE_SOURCE_DIR}"
   -B "${FORGE_TEST_BINARY_DIR}"
   -G "${FORGE_TEST_GENERATOR}"
   -DFORGE_ENABLE_OIDC_AGENT=ON
   -DFORGE_BUILD_PROFILE=full
   -DCMAKE_C_COMPILER=${FORGE_TEST_C_COMPILER}
   -DCMAKE_CXX_COMPILER=${FORGE_TEST_CXX_COMPILER}
)
if(DEFINED FORGE_TEST_OSX_SYSROOT AND NOT "${FORGE_TEST_OSX_SYSROOT}" STREQUAL "")
   list(APPEND _configure_arguments -DCMAKE_OSX_SYSROOT=${FORGE_TEST_OSX_SYSROOT})
endif()
if(DEFINED FORGE_TEST_SYSTEM_NAME AND NOT "${FORGE_TEST_SYSTEM_NAME}" STREQUAL "")
   list(APPEND _configure_arguments -DCMAKE_SYSTEM_NAME=${FORGE_TEST_SYSTEM_NAME})
endif()
if(DEFINED FORGE_TEST_TRY_COMPILE_TARGET_TYPE AND NOT "${FORGE_TEST_TRY_COMPILE_TARGET_TYPE}" STREQUAL "")
   list(APPEND _configure_arguments -DCMAKE_TRY_COMPILE_TARGET_TYPE=${FORGE_TEST_TRY_COMPILE_TARGET_TYPE})
endif()

file(REMOVE_RECURSE "${FORGE_TEST_BINARY_DIR}")
execute_process(
   COMMAND "${CMAKE_COMMAND}" ${_configure_arguments}
   RESULT_VARIABLE _result
   OUTPUT_VARIABLE _stdout
   ERROR_VARIABLE _stderr
)
file(REMOVE_RECURSE "${FORGE_TEST_BINARY_DIR}")

if(_result EQUAL 0)
   message(FATAL_ERROR "FORGE_ENABLE_OIDC_AGENT=ON unexpectedly configured outside Linux")
endif()

set(_output "${_stdout}\n${_stderr}")
if(NOT _output MATCHES "FORGE_ENABLE_OIDC_AGENT=ON is supported only on the Linux full profile")
   message(FATAL_ERROR "Configure failed for an unexpected reason:\n${_output}")
endif()

if(NOT FORGE_PACKAGE_BUILD_DIR)
   message(FATAL_ERROR "FORGE_PACKAGE_BUILD_DIR is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/compile_fail_fixtures.cmake")

foreach(_entry IN LISTS FORGE_DIGEST_COMPILE_FAIL_CASES)
   string(REPLACE "|" ";" _parts "${_entry}")
   list(GET _parts 0 _case)
   list(GET _parts 1 _member)

   execute_process(
      COMMAND
         "${CMAKE_COMMAND}"
         --build
         "${FORGE_PACKAGE_BUILD_DIR}"
         --target
         "forge_digest_negative_${_case}"
         --parallel
         1
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _stdout
      ERROR_VARIABLE _stderr
   )
   set(_output "${_stdout}\n${_stderr}")

   if(_result EQUAL 0)
      message(FATAL_ERROR "${_case}: dangling digest view compiled successfully")
   endif()
   if(NOT _output MATCHES "error: call to deleted member function '${_member}'")
      message(FATAL_ERROR "${_case}: failed for an unrelated reason:\n${_output}")
   endif()
endforeach()

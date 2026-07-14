cmake_minimum_required(VERSION 3.31)

foreach(
   required
   IN ITEMS
      FORGE_PACKAGE_TEST_INSTALL_PREFIX
      FORGE_PACKAGE_TEST_RELOCATED_PREFIX
      FORGE_PACKAGE_TEST_SOURCE_DIR
      FORGE_PACKAGE_TEST_BINARY_DIR
      FORGE_PACKAGE_TEST_SOURCE_ROOT
      FORGE_PACKAGE_TEST_BUILD_ROOT
      FORGE_PACKAGE_TEST_GENERATOR
      FORGE_PACKAGE_TEST_CXX_COMPILER
)
   if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "${required} is required")
   endif()
endforeach()

file(REMOVE_RECURSE "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}")
file(COPY "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}/" DESTINATION "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}")

set(
   forbidden_details
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}/lib/forge/internal/vm_wasm/details"
)
if(EXISTS "${forbidden_details}")
   message(FATAL_ERROR "Installed vm_wasm package exposes private details: ${forbidden_details}")
endif()

set(
   targets_file
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}/lib/cmake/Forge/ForgeTargets.cmake"
)
file(READ "${targets_file}" targets_content)

function(assert_target_has_no_build_paths target)
   set(marker "# Create imported target ${target}")
   string(FIND "${targets_content}" "${marker}" marker_offset)
   if(marker_offset EQUAL -1)
      message(FATAL_ERROR "Installed package does not export ${target}")
   endif()

   string(SUBSTRING "${targets_content}" ${marker_offset} -1 target_tail)
   string(FIND "${target_tail}" "\n# Create imported target " next_target_offset)
   if(next_target_offset EQUAL -1)
      set(target_block "${target_tail}")
   else()
      string(SUBSTRING "${target_tail}" 0 ${next_target_offset} target_block)
   endif()

   foreach(
      forbidden
      IN ITEMS
         "${FORGE_PACKAGE_TEST_SOURCE_ROOT}"
         "${FORGE_PACKAGE_TEST_BUILD_ROOT}"
         "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}"
   )
      string(FIND "${target_block}" "${forbidden}" forbidden_offset)
      if(NOT forbidden_offset EQUAL -1)
         message(FATAL_ERROR "${target} exports build-local path: ${forbidden}")
      endif()
   endforeach()
endfunction()

assert_target_has_no_build_paths("Forge::forge_core")
assert_target_has_no_build_paths("Forge::forge_exceptions")
assert_target_has_no_build_paths("Forge::forge_vm_wasm")
assert_target_has_no_build_paths("Forge::vm_wasm_softfloat_internal")

file(REMOVE_RECURSE "${FORGE_PACKAGE_TEST_BINARY_DIR}")

set(
   configure_command
   "${CMAKE_COMMAND}"
   -S "${FORGE_PACKAGE_TEST_SOURCE_DIR}"
   -B "${FORGE_PACKAGE_TEST_BINARY_DIR}"
   -G "${FORGE_PACKAGE_TEST_GENERATOR}"
   "-DCMAKE_PREFIX_PATH=${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}"
   "-DCMAKE_CXX_COMPILER=${FORGE_PACKAGE_TEST_CXX_COMPILER}"
)
if(DEFINED FORGE_PACKAGE_TEST_OSX_SYSROOT AND NOT "${FORGE_PACKAGE_TEST_OSX_SYSROOT}" STREQUAL "")
   list(APPEND configure_command "-DCMAKE_OSX_SYSROOT=${FORGE_PACKAGE_TEST_OSX_SYSROOT}")
endif()

execute_process(COMMAND ${configure_command} COMMAND_ERROR_IS_FATAL ANY)
execute_process(
   COMMAND "${CMAKE_COMMAND}" --build "${FORGE_PACKAGE_TEST_BINARY_DIR}"
   COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
   COMMAND "${FORGE_PACKAGE_TEST_BINARY_DIR}/forge_package_vm_wasm_component"
   COMMAND_ERROR_IS_FATAL ANY
)

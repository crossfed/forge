cmake_minimum_required(VERSION 3.31)

foreach(
   required
   IN ITEMS
      FORGE_PACKAGE_TEST_INSTALL_PREFIX
      FORGE_PACKAGE_TEST_INSTALL_LIBDIR
      FORGE_PACKAGE_TEST_RELOCATED_PREFIX
      FORGE_PACKAGE_TEST_SOURCE_DIR
      FORGE_PACKAGE_TEST_BINARY_DIR
      FORGE_PACKAGE_TEST_SOURCE_ROOT
      FORGE_PACKAGE_TEST_BUILD_ROOT
      FORGE_PACKAGE_TEST_GENERATOR
      FORGE_PACKAGE_TEST_C_COMPILER
      FORGE_PACKAGE_TEST_CXX_COMPILER
)
   if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "${required} is required")
   endif()
endforeach()

file(
   REMOVE_RECURSE
   "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}"
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}"
   "${FORGE_PACKAGE_TEST_BINARY_DIR}"
)
execute_process(
   COMMAND
      "${CMAKE_COMMAND}" --install "${FORGE_PACKAGE_TEST_BUILD_ROOT}"
      --prefix "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}"
      --component dev
   COMMAND_ERROR_IS_FATAL ANY
)
file(RENAME "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}" "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}")

set(
   targets_file
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}/${FORGE_PACKAGE_TEST_INSTALL_LIBDIR}/cmake/Forge/ForgeTargets.cmake"
)
file(READ "${targets_file}" targets_content)

set(target "Forge::forge_contract_testing")
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

set(
   configure_command
   "${CMAKE_COMMAND}"
   -S "${FORGE_PACKAGE_TEST_SOURCE_DIR}"
   -B "${FORGE_PACKAGE_TEST_BINARY_DIR}"
   -G "${FORGE_PACKAGE_TEST_GENERATOR}"
   "-DCMAKE_PREFIX_PATH=${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}"
   "-DCMAKE_C_COMPILER=${FORGE_PACKAGE_TEST_C_COMPILER}"
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
   COMMAND "${FORGE_PACKAGE_TEST_BINARY_DIR}/forge_package_contract_testing_component"
   COMMAND_ERROR_IS_FATAL ANY
)

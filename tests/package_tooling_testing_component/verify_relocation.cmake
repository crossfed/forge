cmake_minimum_required(VERSION 3.31)

foreach(
   required
   IN ITEMS
      FORGE_PACKAGE_TEST_INSTALL_PREFIX
      FORGE_PACKAGE_TEST_INSTALL_LIBDIR
      FORGE_PACKAGE_TEST_INCLUDEDIR
      FORGE_PACKAGE_TEST_RELOCATED_PREFIX
      FORGE_PACKAGE_TEST_SOURCE_DIR
      FORGE_PACKAGE_TEST_BINARY_DIR
      FORGE_PACKAGE_TEST_SOURCE_ROOT
      FORGE_PACKAGE_TEST_BUILD_ROOT
      FORGE_PACKAGE_TEST_GENERATOR
      FORGE_PACKAGE_TEST_MULTI_CONFIG
      FORGE_PACKAGE_TEST_C_COMPILER
      FORGE_PACKAGE_TEST_CXX_COMPILER
)
   if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "${required} is required")
   endif()
endforeach()
if(FORGE_PACKAGE_TEST_MULTI_CONFIG AND "${FORGE_PACKAGE_TEST_CONFIG}" STREQUAL "")
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_CONFIG is required for a multi-config generator")
endif()
if(FORGE_PACKAGE_TEST_MULTI_CONFIG AND
   (NOT DEFINED FORGE_PACKAGE_TEST_CONFIGURATION_TYPES OR "${FORGE_PACKAGE_TEST_CONFIGURATION_TYPES}" STREQUAL ""))
   message(FATAL_ERROR "FORGE_PACKAGE_TEST_CONFIGURATION_TYPES is required for a multi-config generator")
endif()

file(
   REMOVE_RECURSE
   "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}"
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}"
   "${FORGE_PACKAGE_TEST_BINARY_DIR}"
)
set(
   install_command
   "${CMAKE_COMMAND}" --install "${FORGE_PACKAGE_TEST_BUILD_ROOT}"
   --prefix "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}"
   --component dev
)
if(NOT "${FORGE_PACKAGE_TEST_CONFIG}" STREQUAL "")
   list(APPEND install_command --config "${FORGE_PACKAGE_TEST_CONFIG}")
endif()
execute_process(
   COMMAND ${install_command}
   COMMAND_ERROR_IS_FATAL ANY
)
file(RENAME "${FORGE_PACKAGE_TEST_INSTALL_PREFIX}" "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}")

foreach(legacy_leaf IN ITEMS abi attributes validation manifest testing)
   set(
      legacy_include
      "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}/${FORGE_PACKAGE_TEST_INCLUDEDIR}/forge/contract/${legacy_leaf}"
   )
   if(EXISTS "${legacy_include}")
      message(FATAL_ERROR "Installed package exposes legacy Contract Tooling include path: ${legacy_include}")
   endif()
endforeach()

set(
   targets_file
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}/${FORGE_PACKAGE_TEST_INSTALL_LIBDIR}/cmake/Forge/ForgeTargets.cmake"
)
file(READ "${targets_file}" targets_content)
set(
   config_file
   "${FORGE_PACKAGE_TEST_RELOCATED_PREFIX}/${FORGE_PACKAGE_TEST_INSTALL_LIBDIR}/cmake/Forge/ForgeConfig.cmake"
)
file(READ "${config_file}" config_content)

foreach(tooling_leaf IN ITEMS abi attributes validation manifest testing)
   set(canonical_target "Forge::forge_tooling_${tooling_leaf}")
   set(canonical_marker "# Create imported target ${canonical_target}")
   string(FIND "${targets_content}" "${canonical_marker}" canonical_target_offset)
   if(canonical_target_offset EQUAL -1)
      message(FATAL_ERROR "Installed package does not export ${canonical_target}")
   endif()

   set(canonical_component "tooling_${tooling_leaf}")
   string(FIND "${config_content}" "${canonical_component}" canonical_component_offset)
   if(canonical_component_offset EQUAL -1)
      message(FATAL_ERROR "Installed package does not register ${canonical_component}")
   endif()
endforeach()

foreach(legacy_leaf IN ITEMS abi attributes validation manifest testing)
   set(legacy_target "Forge::forge_contract_${legacy_leaf}")
   set(legacy_component "contract_${legacy_leaf}")
   foreach(content_name IN ITEMS targets_content config_content)
      string(FIND "${${content_name}}" "${legacy_target}" legacy_target_offset)
      if(NOT legacy_target_offset EQUAL -1)
         message(FATAL_ERROR "Installed package retains legacy target ${legacy_target} in ${content_name}")
      endif()
      string(FIND "${${content_name}}" "${legacy_component}" legacy_component_offset)
      if(NOT legacy_component_offset EQUAL -1)
         message(FATAL_ERROR "Installed package retains legacy component ${legacy_component} in ${content_name}")
      endif()
   endforeach()
endforeach()

string(FIND "${config_content}" "Forge_BUILT_WITH_CONTRACT_TOOLING" legacy_tooling_option_offset)
if(NOT legacy_tooling_option_offset EQUAL -1)
   message(FATAL_ERROR "Installed package retains Forge_BUILT_WITH_CONTRACT_TOOLING")
endif()
string(FIND "${config_content}" "Forge_BUILT_WITH_TOOLING" tooling_option_offset)
if(tooling_option_offset EQUAL -1)
   message(FATAL_ERROR "Installed package does not publish Forge_BUILT_WITH_TOOLING")
endif()

set(target "Forge::forge_tooling_testing")
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
if(FORGE_PACKAGE_TEST_MULTI_CONFIG)
   string(REPLACE ";" "\\;" configuration_types "${FORGE_PACKAGE_TEST_CONFIGURATION_TYPES}")
   list(APPEND configure_command "-DCMAKE_CONFIGURATION_TYPES=${configuration_types}")
endif()

execute_process(COMMAND ${configure_command} COMMAND_ERROR_IS_FATAL ANY)
set(build_command "${CMAKE_COMMAND}" --build "${FORGE_PACKAGE_TEST_BINARY_DIR}")
if(NOT "${FORGE_PACKAGE_TEST_CONFIG}" STREQUAL "")
   list(APPEND build_command --config "${FORGE_PACKAGE_TEST_CONFIG}")
endif()
execute_process(
   COMMAND ${build_command}
   COMMAND_ERROR_IS_FATAL ANY
)
set(consumer_executable "${FORGE_PACKAGE_TEST_BINARY_DIR}/forge_package_tooling_testing_component")
if(FORGE_PACKAGE_TEST_MULTI_CONFIG)
   set(
      consumer_executable
      "${FORGE_PACKAGE_TEST_BINARY_DIR}/${FORGE_PACKAGE_TEST_CONFIG}/forge_package_tooling_testing_component"
   )
endif()
execute_process(
   COMMAND "${consumer_executable}"
   COMMAND_ERROR_IS_FATAL ANY
)

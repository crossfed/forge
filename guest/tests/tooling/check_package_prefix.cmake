include(CMakePackageConfigHelpers)

foreach(
   _required
   FORGE_CONTRACT_CONFIG_TEMPLATE
   FORGE_CONTRACT_TOOLCHAIN_TEMPLATE
   FORGE_CONTRACT_FUNCTIONS
   FORGE_CONTRACT_TEST_ROOT
)
   if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
      message(FATAL_ERROR "${_required} is required")
   endif()
endforeach()

set(PROJECT_VERSION 0.0.0)
set(FORGE_CONTRACT_PROFILE developer)
set(FORGE_CONTRACT_REPRODUCIBLE false)
set(FORGE_CONTRACT_MANIFEST_LLVM_VERSION test)
set(FORGE_CONTRACT_MANIFEST_LLVM_COMMIT test)
set(FORGE_CONTRACT_SYSROOT_SCHEMA_VERSION 1)
set(FORGE_CONTRACT_INTRINSIC_VERSION 1)
set(CMAKE_INSTALL_DATADIR share)
set(CMAKE_INSTALL_LIBDIR "lib/x86_64-linux-gnu")
set(CMAKE_SHARED_MODULE_SUFFIX ".so")

set(_prefix "${FORGE_CONTRACT_TEST_ROOT}/prefix")
set(_config_dir "${_prefix}/${CMAKE_INSTALL_LIBDIR}/cmake/ForgeContract")
set(_prefix_anchor "/__forge_contract_prefix")
file(
   RELATIVE_PATH
   FORGE_CONTRACT_PREFIX_FROM_CONFIG_DIR
   "${_prefix_anchor}/${CMAKE_INSTALL_LIBDIR}/cmake/ForgeContract"
   "${_prefix_anchor}"
)

file(REMOVE_RECURSE "${FORGE_CONTRACT_TEST_ROOT}")
file(MAKE_DIRECTORY "${_config_dir}")
configure_package_config_file(
   "${FORGE_CONTRACT_CONFIG_TEMPLATE}"
   "${_config_dir}/ForgeContractConfig.cmake"
   INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ForgeContract"
   INSTALL_PREFIX "${_prefix}"
   NO_SET_AND_CHECK_MACRO
   NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
configure_file(
   "${FORGE_CONTRACT_TOOLCHAIN_TEMPLATE}"
   "${_config_dir}/ForgeContractToolchain.cmake"
   @ONLY
)
configure_file("${FORGE_CONTRACT_FUNCTIONS}" "${_config_dir}/ForgeContractFunctions.cmake" COPYONLY)

file(MAKE_DIRECTORY "${_prefix}/bin")
foreach(_tool clang++ wasm-ld abigen contract-check contract-manifest)
   file(WRITE "${_prefix}/bin/${_tool}" "")
endforeach()
file(MAKE_DIRECTORY "${_prefix}/sysroot")
file(MAKE_DIRECTORY "${_prefix}/${CMAKE_INSTALL_DATADIR}/forge-contract")
file(WRITE "${_prefix}/${CMAKE_INSTALL_DATADIR}/forge-contract/sysroot.sha256" "test\n")
file(MAKE_DIRECTORY "${_prefix}/${CMAKE_INSTALL_LIBDIR}/forge-contract")
file(WRITE "${_prefix}/${CMAKE_INSTALL_LIBDIR}/forge-contract/attr-plugin${CMAKE_SHARED_MODULE_SUFFIX}" "")

set(_consumer "${FORGE_CONTRACT_TEST_ROOT}/consumer")
file(MAKE_DIRECTORY "${_consumer}")
file(
   WRITE
   "${_consumer}/CMakeLists.txt"
   [=[
cmake_minimum_required(VERSION 3.31)
project(ForgeContractPackagePrefixTest NONE)
find_package(ForgeContract CONFIG REQUIRED)

if(NOT "${ForgeContract_PREFIX}" STREQUAL "${EXPECTED_PREFIX}")
   message(FATAL_ERROR "ForgeContractConfig resolved the wrong prefix: ${ForgeContract_PREFIX}")
endif()
if(NOT "${ForgeContract_ATTR_PLUGIN}" STREQUAL "${EXPECTED_PLUGIN}")
   message(FATAL_ERROR "ForgeContractConfig resolved the wrong plugin: ${ForgeContract_ATTR_PLUGIN}")
endif()
if(NOT "${CMAKE_CXX_COMPILER}" STREQUAL "${EXPECTED_PREFIX}/bin/clang++")
   message(FATAL_ERROR "ForgeContractToolchain resolved the wrong compiler: ${CMAKE_CXX_COMPILER}")
endif()
]=]
)

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -S "${_consumer}"
      -B "${FORGE_CONTRACT_TEST_ROOT}/build"
      -DForgeContract_DIR=${_config_dir}
      -DCMAKE_TOOLCHAIN_FILE=${_config_dir}/ForgeContractToolchain.cmake
      -DEXPECTED_PREFIX=${_prefix}
      -DEXPECTED_PLUGIN=${_prefix}/${CMAKE_INSTALL_LIBDIR}/forge-contract/attr-plugin${CMAKE_SHARED_MODULE_SUFFIX}
   RESULT_VARIABLE _configure_result
)
if(NOT _configure_result EQUAL 0)
   message(FATAL_ERROR "nested-libdir ForgeContract consumer configuration failed")
endif()

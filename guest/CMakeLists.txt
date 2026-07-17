cmake_minimum_required(VERSION 3.31)

file(
   STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt"
   _forge_project_declaration
   REGEX "^project\\(forge VERSION [0-9]+\\.[0-9]+\\.[0-9]+ "
   LIMIT_COUNT 1
)
if(NOT _forge_project_declaration MATCHES "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)")
   message(FATAL_ERROR "cannot determine Forge version for the Contract SDK")
endif()

project(ForgeContractSDK VERSION "${CMAKE_MATCH_1}" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(ExternalProject)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(FORGE_CONTRACT_PROFILE "developer" CACHE STRING "Contract SDK profile: developer or release")
set_property(CACHE FORGE_CONTRACT_PROFILE PROPERTY STRINGS developer release)
set(FORGE_CONTRACT_FORGE_DIR "" CACHE PATH "Directory containing ForgeConfig.cmake for host tools")
set(FORGE_CONTRACT_SYSROOT "" CACHE PATH "Existing wasm32 sysroot used by developer mode")
set(FORGE_CONTRACT_CLANG "" CACHE FILEPATH "Developer-mode clang++")
set(FORGE_CONTRACT_WASM_LD "" CACHE FILEPATH "Developer-mode wasm-ld")
set(FORGE_CONTRACT_LLVM_AR "" CACHE FILEPATH "Developer-mode llvm-ar")
set(FORGE_CONTRACT_LLVM_RANLIB "" CACHE FILEPATH "Developer-mode llvm-ranlib")
set(FORGE_CONTRACT_LLVM_SOURCE_DIR "" CACHE PATH "Existing pinned LLVM source checkout used by release mode")
set(FORGE_CONTRACT_JOBS 4 CACHE STRING "Parallel jobs used by SDK superbuilds")

set(FORGE_CONTRACT_LLVM_TAG "llvmorg-22.1.8")
set(FORGE_CONTRACT_LLVM_COMMIT "ca7933e47d3a3451d81e72ac174dcb5aa28b59d1")
set(FORGE_CONTRACT_MANIFEST_LLVM_VERSION "${FORGE_CONTRACT_LLVM_TAG}")
set(FORGE_CONTRACT_MANIFEST_LLVM_COMMIT "${FORGE_CONTRACT_LLVM_COMMIT}")
set(FORGE_CONTRACT_SYSROOT_SCHEMA_VERSION 1)
set(FORGE_CONTRACT_INTRINSIC_VERSION 1)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(FORGE_CONTRACT_PROFILE STREQUAL "release")
   set(_llvm_install "${CMAKE_BINARY_DIR}/toolchain")
   if(FORGE_CONTRACT_LLVM_SOURCE_DIR)
      file(REAL_PATH "${FORGE_CONTRACT_LLVM_SOURCE_DIR}" _llvm_source)
      if(NOT EXISTS "${_llvm_source}/llvm/CMakeLists.txt")
         message(FATAL_ERROR "FORGE_CONTRACT_LLVM_SOURCE_DIR is not an llvm-project checkout: ${_llvm_source}")
      endif()
      find_package(Git REQUIRED)
      execute_process(
         COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
         WORKING_DIRECTORY "${_llvm_source}"
         OUTPUT_VARIABLE _llvm_source_commit
         OUTPUT_STRIP_TRAILING_WHITESPACE
         COMMAND_ERROR_IS_FATAL ANY
      )
      if(NOT _llvm_source_commit STREQUAL FORGE_CONTRACT_LLVM_COMMIT)
         message(
            FATAL_ERROR
            "release LLVM checkout is ${_llvm_source_commit}, expected ${FORGE_CONTRACT_LLVM_COMMIT}"
         )
      endif()
      set(
         _llvm_source_arguments
         SOURCE_DIR "${_llvm_source}"
         DOWNLOAD_COMMAND ""
         UPDATE_COMMAND ""
         PATCH_COMMAND ""
      )
   else()
      set(_llvm_source "${CMAKE_BINARY_DIR}/forge_contract_llvm-prefix/src/forge_contract_llvm")
      set(
         _llvm_source_arguments
         GIT_REPOSITORY https://github.com/llvm/llvm-project.git
         GIT_TAG ${FORGE_CONTRACT_LLVM_COMMIT}
         GIT_SHALLOW FALSE
      )
   endif()
   if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
      set(_llvm_host_target AArch64)
   else()
      set(_llvm_host_target X86)
   endif()
   ExternalProject_Add(
      forge_contract_llvm
      ${_llvm_source_arguments}
      SOURCE_SUBDIR llvm
      CMAKE_GENERATOR Ninja
      CMAKE_ARGS
         -DCMAKE_BUILD_TYPE=Release
         -DCMAKE_INSTALL_PREFIX=${_llvm_install}
         -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
         -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
         -DLLVM_ENABLE_PROJECTS=clang\;lld
         -DLLVM_TARGETS_TO_BUILD=WebAssembly\;${_llvm_host_target}
         -DLLVM_BUILD_LLVM_DYLIB=ON
         -DLLVM_LINK_LLVM_DYLIB=ON
         -DCLANG_LINK_CLANG_DYLIB=ON
         -DLLVM_ENABLE_TERMINFO=OFF
         -DLLVM_ENABLE_ZLIB=OFF
         -DLLVM_ENABLE_ZSTD=OFF
         -DLLVM_INCLUDE_TESTS=OFF
         -DLLVM_INCLUDE_EXAMPLES=OFF
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> -j ${FORGE_CONTRACT_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install -j ${FORGE_CONTRACT_JOBS}
   )

   set(FORGE_CONTRACT_CLANG "${_llvm_install}/bin/clang++")
   set(FORGE_CONTRACT_CLANG_SCAN_DEPS "${_llvm_install}/bin/clang-scan-deps")
   set(FORGE_CONTRACT_WASM_LD "${_llvm_install}/bin/wasm-ld")
   set(FORGE_CONTRACT_LLVM_AR "${_llvm_install}/bin/llvm-ar")
   set(FORGE_CONTRACT_LLVM_RANLIB "${_llvm_install}/bin/llvm-ranlib")
   set(FORGE_CONTRACT_CLANG_RESOURCE_DIR "${_llvm_install}/lib/clang/22")
   set(FORGE_CONTRACT_SYSROOT "${CMAKE_BINARY_DIR}/sysroot-base")

   ExternalProject_Add(
      forge_contract_sysroot
      DEPENDS forge_contract_llvm
      SOURCE_DIR "${_llvm_source}/runtimes"
      DOWNLOAD_COMMAND ""
      UPDATE_COMMAND ""
      PATCH_COMMAND ""
      CMAKE_GENERATOR Ninja
      CMAKE_ARGS
         -DCMAKE_BUILD_TYPE=Release
         -DCMAKE_INSTALL_PREFIX=${FORGE_CONTRACT_SYSROOT}
         -DCMAKE_SYSTEM_NAME=Generic
         -DCMAKE_SYSROOT=${FORGE_CONTRACT_SYSROOT}
         -DCMAKE_C_COMPILER=${_llvm_install}/bin/clang
         -DCMAKE_CXX_COMPILER=${_llvm_install}/bin/clang++
         -DCMAKE_ASM_COMPILER=${_llvm_install}/bin/clang
         -DCMAKE_C_COMPILER_TARGET=wasm32
         -DCMAKE_CXX_COMPILER_TARGET=wasm32
         -DCMAKE_ASM_COMPILER_TARGET=wasm32
         -DCMAKE_AR=${_llvm_install}/bin/llvm-ar
         -DCMAKE_RANLIB=${_llvm_install}/bin/llvm-ranlib
         -DCMAKE_ASM_COMPILER_AR=${_llvm_install}/bin/llvm-ar
         -DCMAKE_ASM_COMPILER_RANLIB=${_llvm_install}/bin/llvm-ranlib
         -DCMAKE_C_FLAGS=-mcpu=mvp
         -DCMAKE_CXX_FLAGS=-mcpu=mvp
         -DCMAKE_ASM_FLAGS=-mcpu=mvp
         -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
         -DLLVM_ENABLE_RUNTIMES=libcxx\;libcxxabi\;compiler-rt
         -DLIBCXX_ENABLE_EXCEPTIONS=OFF
         -DLIBCXX_ENABLE_RTTI=OFF
         -DLIBCXX_ENABLE_THREADS=OFF
         -DLIBCXX_ENABLE_FILESYSTEM=OFF
         -DLIBCXX_ENABLE_LOCALIZATION=OFF
         -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF
         -DLIBCXX_ENABLE_MONOTONIC_CLOCK=OFF
         -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF
         -DLIBCXX_ENABLE_UNICODE=OFF
         -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF
         -DLIBCXX_HAS_TERMINAL_AVAILABLE=OFF
         -DLIBCXX_ENABLE_SHARED=OFF
         -DLIBCXX_ENABLE_STATIC=ON
         -DLIBCXX_SHARED_OUTPUT_NAME=c++-shared-disabled
         -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON
         -DLIBCXX_ENABLE_NEW_DELETE_DEFINITIONS=OFF
         -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF
         -DLIBCXXABI_ENABLE_THREADS=OFF
         -DLIBCXXABI_ENABLE_SHARED=OFF
         -DLIBCXXABI_ENABLE_STATIC=ON
         -DLIBCXXABI_SHARED_OUTPUT_NAME=c++abi-shared-disabled
         -DLIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS=OFF
         -DLIBCXXABI_BAREMETAL=ON
         -DLIBCXXABI_USE_LLVM_UNWINDER=OFF
         -DCOMPILER_RT_BAREMETAL_BUILD=ON
         -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
         -DCOMPILER_RT_BUILD_BUILTINS=ON
         -DCOMPILER_RT_BUILD_SANITIZERS=OFF
         -DCOMPILER_RT_BUILD_XRAY=OFF
         -DCOMPILER_RT_BUILD_LIBFUZZER=OFF
         -DCOMPILER_RT_BUILD_PROFILE=OFF
         -DCOMPILER_RT_INCLUDE_TESTS=OFF
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> -j ${FORGE_CONTRACT_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install -j ${FORGE_CONTRACT_JOBS}
   )
   ExternalProject_Add_Step(
      forge_contract_sysroot stage_guest_headers
      COMMAND ${CMAKE_COMMAND} -E make_directory "${FORGE_CONTRACT_SYSROOT}/include"
      COMMAND
         ${CMAKE_COMMAND} -E copy_directory
         "${CMAKE_CURRENT_SOURCE_DIR}/sysroot/include"
         "${FORGE_CONTRACT_SYSROOT}/include"
      DEPENDEES download
      DEPENDERS configure
   )
   if(NOT FORGE_CONTRACT_FORGE_DIR)
      message(FATAL_ERROR "Release profile requires -DFORGE_CONTRACT_FORGE_DIR=<installed Forge package>")
   endif()
   set(_host_tools_install "${CMAKE_BINARY_DIR}/host-tools")
   string(REPLACE ";" "|" _forge_contract_prefix_path "${CMAKE_PREFIX_PATH}")
   ExternalProject_Add(
      forge_contract_tools
      DEPENDS forge_contract_llvm
      SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
      CMAKE_GENERATOR Ninja
      LIST_SEPARATOR "|"
      CMAKE_ARGS
         -DCMAKE_BUILD_TYPE=Release
         -DCMAKE_INSTALL_PREFIX=${_host_tools_install}
         -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
         -DCMAKE_C_COMPILER=${_llvm_install}/bin/clang
         -DCMAKE_CXX_COMPILER=${_llvm_install}/bin/clang++
         -DCMAKE_PREFIX_PATH:PATH=${_forge_contract_prefix_path}
         -DClang_DIR=${_llvm_install}/lib/cmake/clang
         -DLLVM_DIR=${_llvm_install}/lib/cmake/llvm
         -DForge_DIR=${FORGE_CONTRACT_FORGE_DIR}
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> -j ${FORGE_CONTRACT_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install -j ${FORGE_CONTRACT_JOBS}
   )
   set(FORGE_CONTRACT_REPRODUCIBLE true)
else()
   find_package(Clang 22.1 CONFIG REQUIRED)
   find_library(
      FORGE_CONTRACT_CLANG_CPP_RUNTIME
      NAMES clang-cpp
      HINTS ${CLANG_LIBRARY_DIRS} ${LLVM_LIBRARY_DIRS}
      REQUIRED
      NO_DEFAULT_PATH
   )
   find_library(
      FORGE_CONTRACT_LLVM_RUNTIME
      NAMES LLVM
      HINTS ${LLVM_LIBRARY_DIRS}
      REQUIRED
      NO_DEFAULT_PATH
   )
   include(cmake/select_developer_toolchain.cmake)
   forge_contract_select_developer_toolchain()
   foreach(
      _tool
      FORGE_CONTRACT_CLANG
      FORGE_CONTRACT_CLANG_SCAN_DEPS
      FORGE_CONTRACT_WASM_LD
      FORGE_CONTRACT_LLVM_AR
      FORGE_CONTRACT_LLVM_RANLIB
   )
      if(NOT EXISTS "${${_tool}}" OR IS_DIRECTORY "${${_tool}}")
         message(FATAL_ERROR "Developer profile requires a valid ${_tool}: ${${_tool}}")
      endif()
   endforeach()
   if(NOT FORGE_CONTRACT_SYSROOT OR NOT IS_DIRECTORY "${FORGE_CONTRACT_SYSROOT}")
      message(FATAL_ERROR "Developer profile requires -DFORGE_CONTRACT_SYSROOT=<wasm32 sysroot>")
   endif()
   execute_process(
      COMMAND "${FORGE_CONTRACT_CLANG}" -print-resource-dir
      OUTPUT_VARIABLE FORGE_CONTRACT_CLANG_RESOURCE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY
   )
   execute_process(
      COMMAND "${FORGE_CONTRACT_CLANG}" --version
      OUTPUT_VARIABLE _forge_contract_clang_version
      OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY
   )
   string(REGEX MATCH "^[^\r\n]+" FORGE_CONTRACT_MANIFEST_LLVM_VERSION "${_forge_contract_clang_version}")
   if(FORGE_CONTRACT_MANIFEST_LLVM_VERSION STREQUAL "")
      message(FATAL_ERROR "cannot determine the selected developer Clang version")
   endif()
   set(FORGE_CONTRACT_MANIFEST_LLVM_COMMIT "")
   set(FORGE_CONTRACT_REPRODUCIBLE false)
   if(FORGE_CONTRACT_FORGE_DIR)
      find_package(
         Forge CONFIG REQUIRED
         COMPONENTS
            chain_protocol
            codec_json
            crypto
            vm_wasm
            contract_abi
            contract_attributes
            contract_validation
            contract_manifest
         PATHS "${FORGE_CONTRACT_FORGE_DIR}"
         NO_DEFAULT_PATH
      )
   else()
      find_package(
         Forge CONFIG REQUIRED
         COMPONENTS
            chain_protocol
            codec_json
            crypto
            vm_wasm
            contract_abi
            contract_attributes
            contract_validation
            contract_manifest
      )
   endif()
   add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tools" "${CMAKE_CURRENT_BINARY_DIR}/tools")
endif()

set(_forge_contract_input_sysroot "${FORGE_CONTRACT_SYSROOT}")
set(FORGE_CONTRACT_SYSROOT "${CMAKE_BINARY_DIR}/sdk-sysroot")
set(_forge_contract_sysroot_stage_stamp "${CMAKE_BINARY_DIR}/forge-contract-sysroot.stamp")
set(_forge_contract_sysroot_stage_dependencies)
if(TARGET forge_contract_sysroot)
   list(APPEND _forge_contract_sysroot_stage_dependencies forge_contract_sysroot)
endif()
add_custom_command(
   OUTPUT "${_forge_contract_sysroot_stage_stamp}"
   COMMAND
      ${CMAKE_COMMAND}
      -DSOURCE=${_forge_contract_input_sysroot}
      -DDESTINATION=${FORGE_CONTRACT_SYSROOT}
      -DSTAMP=${_forge_contract_sysroot_stage_stamp}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stage_sysroot.cmake"
   DEPENDS
      ${_forge_contract_sysroot_stage_dependencies}
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stage_sysroot.cmake"
   VERBATIM
)
add_custom_target(forge_contract_stage_sysroot DEPENDS "${_forge_contract_sysroot_stage_stamp}")

set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_sysroot_hash "${_generated_dir}/sysroot.sha256")
set(_intrinsic_registry "${CMAKE_CURRENT_SOURCE_DIR}/libraries/contract/include/forge/contract/intrinsics.hpp")
set(_intrinsic_manifest "${_generated_dir}/intrinsics.json")
set(_forge_intrinsics "${_generated_dir}/include/forge/contract/intrinsics.h")
set(_eosio_intrinsics "${_generated_dir}/include/eosio/action.h")
set(_eosio_db_intrinsics "${_generated_dir}/include/eosio/db.h")
add_custom_command(
   OUTPUT "${_intrinsic_manifest}" "${_forge_intrinsics}" "${_eosio_intrinsics}" "${_eosio_db_intrinsics}"
   COMMAND
      ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_intrinsic_manifest.py"
      --registry "${_intrinsic_registry}"
      --output "${_intrinsic_manifest}"
      --forge-header "${_forge_intrinsics}"
      --eosio-header "${_eosio_intrinsics}"
      --eosio-db-header "${_eosio_db_intrinsics}"
   DEPENDS "${_intrinsic_registry}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_intrinsic_manifest.py"
   VERBATIM
)
add_custom_target(
   forge_contract_intrinsic_manifest ALL
   DEPENDS "${_intrinsic_manifest}" "${_forge_intrinsics}" "${_eosio_intrinsics}" "${_eosio_db_intrinsics}"
)

ExternalProject_Add(
   forge_contract_guest_runtime
   DEPENDS forge_contract_stage_sysroot forge_contract_intrinsic_manifest
   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/runtime"
   DOWNLOAD_COMMAND ""
   UPDATE_COMMAND ""
   PATCH_COMMAND ""
   CMAKE_GENERATOR Ninja
   CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
      -DCMAKE_INSTALL_PREFIX=${FORGE_CONTRACT_SYSROOT}
      -DCMAKE_SYSTEM_NAME=Generic
      -DCMAKE_SYSTEM_PROCESSOR=wasm32
      -DCMAKE_SYSROOT=${FORGE_CONTRACT_SYSROOT}
      -DCMAKE_CXX_COMPILER=${FORGE_CONTRACT_CLANG}
      -DCMAKE_CXX_COMPILER_TARGET=wasm32
      -DCMAKE_AR=${FORGE_CONTRACT_LLVM_AR}
      -DCMAKE_RANLIB=${FORGE_CONTRACT_LLVM_RANLIB}
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
      -DFORGE_GUEST_INCLUDE_DIR=${_generated_dir}/include
   BUILD_ALWAYS TRUE
   BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> -j ${FORGE_CONTRACT_JOBS}
   INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
)

add_custom_target(
   forge_contract_sysroot_hash ALL
   COMMAND
      ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hash_tree.py"
      --root "${FORGE_CONTRACT_SYSROOT}"
      --output "${_sysroot_hash}"
   DEPENDS forge_contract_guest_runtime "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hash_tree.py"
   BYPRODUCTS "${_sysroot_hash}"
   VERBATIM
)

configure_file(cmake/host.hpp.in "${_generated_dir}/include/forge/contract/host.hpp" COPYONLY)

set(_forge_contract_prefix_anchor "/__forge_contract_prefix")
set(_forge_contract_config_install_dir
    "${_forge_contract_prefix_anchor}/${CMAKE_INSTALL_LIBDIR}/cmake/ForgeContract")
file(
   RELATIVE_PATH
   FORGE_CONTRACT_PREFIX_FROM_CONFIG_DIR
   "${_forge_contract_config_install_dir}"
   "${_forge_contract_prefix_anchor}"
)
configure_file(
   cmake/ForgeContractToolchain.cmake.in
   "${CMAKE_CURRENT_BINARY_DIR}/ForgeContractToolchain.cmake"
   @ONLY
)
configure_package_config_file(
   cmake/ForgeContractConfig.cmake.in
   "${CMAKE_CURRENT_BINARY_DIR}/ForgeContractConfig.cmake"
   INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ForgeContract"
   NO_SET_AND_CHECK_MACRO
   NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
write_basic_package_version_file(
   "${CMAKE_CURRENT_BINARY_DIR}/ForgeContractConfigVersion.cmake"
   VERSION ${PROJECT_VERSION}
   COMPATIBILITY SameMajorVersion
)

install(
   FILES
      "${CMAKE_CURRENT_BINARY_DIR}/ForgeContractConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/ForgeContractConfigVersion.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/ForgeContractToolchain.cmake"
      cmake/ForgeContractFunctions.cmake
   DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ForgeContract"
)
install(DIRECTORY build DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract")
install(DIRECTORY libraries/contract/include/ DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/modules")
install(
   FILES
      libraries/contract/include/forge/contract/intrinsics.hpp
      libraries/contract/include/forge/contract/serialize.hpp
   DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/include/forge/contract"
)
install(DIRECTORY libraries/eosio/include/ DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/include")
install(DIRECTORY "${_generated_dir}/include/" DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/include")
install(
   FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/raw/stream.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/raw/codec.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/chain/protocol/values.cpp"
      libraries/contract/dispatcher.cpp
   DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/runtime"
)
install(
   FILES "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/exceptions/include/forge/exceptions/policy.hpp"
   DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/include/forge/exceptions"
)
install(DIRECTORY examples/ DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/examples")
install(FILES README.md libraries/runtime/PROVENANCE.md DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/forge-contract")
install(
   FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
      "${CMAKE_CURRENT_SOURCE_DIR}/../NOTICE"
      "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_LICENSES"
      "${CMAKE_CURRENT_SOURCE_DIR}/../PROVENANCE.md"
   DESTINATION .
)
install(FILES "${_intrinsic_manifest}" DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/registry")
install(FILES "${_sysroot_hash}" DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract")

install(
   FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/raw/include/forge/raw/stream.cppm"
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/raw/include/forge/raw/varint_value.cppm"
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/raw/include/forge/raw/codec.cppm"
   DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/modules/forge/raw"
)
install(
   FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/chain/protocol/include/forge/chain/protocol/values.cppm"
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/chain/protocol/include/forge/chain/protocol/fixed_key.cppm"
      "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/chain/protocol/include/forge/chain/protocol/fixed_key_value.cppm"
   DESTINATION "${CMAKE_INSTALL_DATADIR}/forge-contract/modules/forge/chain/protocol"
)

install(DIRECTORY "${FORGE_CONTRACT_SYSROOT}/" DESTINATION sysroot)
install(DIRECTORY "${FORGE_CONTRACT_CLANG_RESOURCE_DIR}/" DESTINATION "lib/clang/22")
install(
   CODE
      "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
       file(COPY_FILE \"${FORGE_CONTRACT_CLANG}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/clang++\" ONLY_IF_DIFFERENT)
       file(COPY_FILE \"${FORGE_CONTRACT_CLANG_SCAN_DEPS}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/clang-scan-deps\" ONLY_IF_DIFFERENT)
       file(COPY_FILE \"${FORGE_CONTRACT_LLVM_AR}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/llvm-ar\" ONLY_IF_DIFFERENT)
       file(COPY_FILE \"${FORGE_CONTRACT_LLVM_RANLIB}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/llvm-ranlib\" ONLY_IF_DIFFERENT)"
)
if(EXISTS "${FORGE_CONTRACT_WASM_LD}")
   file(REAL_PATH "${FORGE_CONTRACT_WASM_LD}" _forge_contract_wasm_ld)
else()
   set(_forge_contract_wasm_ld "${FORGE_CONTRACT_WASM_LD}")
endif()
install(
   CODE
      "file(REMOVE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/wasm-ld\")
       file(COPY_FILE \"${_forge_contract_wasm_ld}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/wasm-ld\")
       file(CHMOD \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/wasm-ld\"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)"
)
if(NOT FORGE_CONTRACT_PROFILE STREQUAL "release")
   function(_forge_contract_install_runtime path)
      file(REAL_PATH "${path}" _runtime_path)
      get_filename_component(_runtime_name "${path}" NAME)
      get_filename_component(_real_name "${_runtime_path}" NAME)
      install(FILES "${_runtime_path}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
      if(NOT "${_runtime_name}" STREQUAL "${_real_name}")
         install(
            CODE
               "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\")
                file(REMOVE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${_runtime_name}\")
                file(CREATE_LINK \"${_real_name}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${_runtime_name}\" SYMBOLIC)"
         )
      endif()
   endfunction()

   _forge_contract_install_runtime("${FORGE_CONTRACT_CLANG_CPP_RUNTIME}")
   _forge_contract_install_runtime("${FORGE_CONTRACT_LLVM_RUNTIME}")
   get_filename_component(_forge_contract_wasm_ld_bin "${_forge_contract_wasm_ld}" DIRECTORY)
   file(GLOB _forge_contract_lld_runtime LIST_DIRECTORIES FALSE
        "${_forge_contract_wasm_ld_bin}/../lib/liblld*${CMAKE_SHARED_LIBRARY_SUFFIX}")
   foreach(_runtime IN LISTS _forge_contract_lld_runtime)
      _forge_contract_install_runtime("${_runtime}")
   endforeach()
endif()
configure_file(
   cmake/BundleContractSDK.cmake.in
   "${CMAKE_CURRENT_BINARY_DIR}/BundleContractSDK.cmake"
   @ONLY
)
configure_file(
   cmake/ContractSDKRuntimeDependencies.cmake
   "${CMAKE_CURRENT_BINARY_DIR}/ContractSDKRuntimeDependencies.cmake"
   COPYONLY
)
if(FORGE_CONTRACT_PROFILE STREQUAL "release")
   install(
      DIRECTORY "${_llvm_install}/lib/"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      FILES_MATCHING
         PATTERN "libLLVM*"
         PATTERN "libclang-cpp*"
         PATTERN "liblld*"
   )
   install(DIRECTORY "${_host_tools_install}/bin/" DESTINATION bin USE_SOURCE_PERMISSIONS)
   install(DIRECTORY "${_host_tools_install}/${CMAKE_INSTALL_LIBDIR}/forge-contract/"
           DESTINATION "${CMAKE_INSTALL_LIBDIR}/forge-contract")
   install(
      FILES
         "${_llvm_source}/llvm/LICENSE.TXT"
         "${_llvm_source}/libcxx/LICENSE.TXT"
         "${_llvm_source}/compiler-rt/LICENSE.TXT"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/forge-contract"
   )
endif()
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleContractSDK.cmake")

if(APPLE)
   set(_forge_contract_package_os macos)
else()
   string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _forge_contract_package_os)
endif()
string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _forge_contract_package_arch)
set(_forge_contract_package_file_name
    "forge-contract-sdk-${PROJECT_VERSION}-${_forge_contract_package_os}-${_forge_contract_package_arch}")
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_FILE_NAME "${_forge_contract_package_file_name}")
include(CPack)

set(_forge_contract_archive "${CMAKE_CURRENT_BINARY_DIR}/${_forge_contract_package_file_name}.tar.gz")
set(_forge_contract_archive_dependencies forge_contract_sysroot_hash forge_contract_intrinsic_manifest)
if(TARGET forge_contract_tools)
   list(APPEND _forge_contract_archive_dependencies forge_contract_tools)
else()
   list(
      APPEND _forge_contract_archive_dependencies
      attr_plugin
      abigen
      contract_check
      contract_manifest
   )
endif()
add_custom_target(
   forge_contract_sdk_archive
   COMMAND "${CMAKE_CPACK_COMMAND}" -G TGZ --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hash_file.py"
      --input "${_forge_contract_archive}"
      --output "${_forge_contract_archive}.sha256"
   DEPENDS ${_forge_contract_archive_dependencies}
   BYPRODUCTS "${_forge_contract_archive}" "${_forge_contract_archive}.sha256"
   VERBATIM
)

add_custom_target(
   forge_contract_sdk_relocation
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/run_relocation.py"
      --archive "${_forge_contract_archive}"
      --cmake "${CMAKE_COMMAND}"
      --source-root "${CMAKE_CURRENT_SOURCE_DIR}/.."
      --output "${CMAKE_CURRENT_BINARY_DIR}/relocation"
   DEPENDS forge_contract_sdk_archive
   VERBATIM
)

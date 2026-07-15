include(ExternalProject)

function(forge_add_contract target)
   cmake_parse_arguments(ARG "" "RICARDIAN_CONTRACTS;RICARDIAN_CLAUSES" "SOURCES" ${ARGN})
   if(NOT ARG_SOURCES)
      message(FATAL_ERROR "forge_add_contract(${target}) requires SOURCES")
   endif()
   if(TARGET ${target})
      message(FATAL_ERROR "forge_add_contract target already exists: ${target}")
   endif()

   set(_sources)
   foreach(_source IN LISTS ARG_SOURCES)
      get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
      if(NOT EXISTS "${_absolute}")
         message(FATAL_ERROR "contract source does not exist: ${_absolute}")
      endif()
      list(APPEND _sources "${_absolute}")
   endforeach()
   string(JOIN "|" _encoded_sources ${_sources})

   set(_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract")
   ExternalProject_Add(
      ${target}
      SOURCE_DIR "${ForgeContract_DATA_DIR}/build"
      BINARY_DIR "${_binary_dir}"
      DOWNLOAD_COMMAND ""
      UPDATE_COMMAND ""
      PATCH_COMMAND ""
      INSTALL_COMMAND ""
      BUILD_ALWAYS TRUE
      CMAKE_GENERATOR "${CMAKE_GENERATOR}"
      CMAKE_ARGS
         -DCMAKE_TOOLCHAIN_FILE=${ForgeContract_TOOLCHAIN}
         -DFORGE_CONTRACT_NAME=${target}
         -DFORGE_CONTRACT_SOURCES_ENCODED=${_encoded_sources}
         -DFORGE_CONTRACT_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
         -DFORGE_CONTRACT_DATA_DIR=${ForgeContract_DATA_DIR}
         -DFORGE_CONTRACT_ABIGEN=${ForgeContract_ABIGEN}
         -DFORGE_CONTRACT_ATTR_PLUGIN=${ForgeContract_ATTR_PLUGIN}
         -DFORGE_CONTRACT_CHECK=${ForgeContract_CHECK}
         -DFORGE_CONTRACT_MANIFEST=${ForgeContract_MANIFEST}
         -DFORGE_CONTRACT_SDK_VERSION=${ForgeContract_VERSION}
         -DFORGE_CONTRACT_LLVM_VERSION=${ForgeContract_LLVM_VERSION}
         -DFORGE_CONTRACT_LLVM_COMMIT=${ForgeContract_LLVM_COMMIT}
         -DFORGE_CONTRACT_SYSROOT_SCHEMA_VERSION=${ForgeContract_SYSROOT_SCHEMA_VERSION}
         -DFORGE_CONTRACT_SYSROOT_HASH=${ForgeContract_SYSROOT_HASH}
         -DFORGE_CONTRACT_INTRINSIC_VERSION=${ForgeContract_INTRINSIC_VERSION}
         -DFORGE_CONTRACT_PROFILE=${ForgeContract_PROFILE}
         -DFORGE_CONTRACT_REPRODUCIBLE=${ForgeContract_REPRODUCIBLE}
         -DFORGE_CONTRACT_RICARDIAN_CONTRACTS=${ARG_RICARDIAN_CONTRACTS}
         -DFORGE_CONTRACT_RICARDIAN_CLAUSES=${ARG_RICARDIAN_CLAUSES}
      BUILD_BYPRODUCTS
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.wasm"
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.abi"
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract.json"
   )
endfunction()

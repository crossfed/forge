include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/ForgeContractGraph.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ForgeContractBuild.cmake")

function(forge_add_contract_project target)
   if(FORGE_CONTRACT_GUEST)
      message(
         FATAL_ERROR
         "forge_add_contract_project(${target}) is a host-side launcher"
      )
   endif()
   cmake_parse_arguments(
      ARG
      ""
      "SOURCE_DIR;BINARY_DIR;CONTRACT"
      ""
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(
         FATAL_ERROR
         "forge_add_contract_project(${target}) received unknown arguments: "
         "${ARG_UNPARSED_ARGUMENTS}"
      )
   endif()
   foreach(_required SOURCE_DIR BINARY_DIR CONTRACT)
      if(NOT ARG_${_required})
         message(
            FATAL_ERROR
            "forge_add_contract_project(${target}) requires ${_required}"
         )
      endif()
   endforeach()
   if(TARGET "${target}")
      message(FATAL_ERROR "forge_add_contract_project target already exists: ${target}")
   endif()

   get_filename_component(
      _source_dir "${ARG_SOURCE_DIR}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
   )
   get_filename_component(
      _binary_dir "${ARG_BINARY_DIR}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
   )
   if(NOT EXISTS "${_source_dir}/CMakeLists.txt")
      message(
         FATAL_ERROR
         "Forge Contract guest project has no CMakeLists.txt: ${_source_dir}"
      )
   endif()
   if(_source_dir STREQUAL _binary_dir)
      message(FATAL_ERROR "Forge Contract guest source and binary directories must differ")
   endif()

   set(_artifact_dir "${_binary_dir}/artifacts")
   set(_prefix_path ${CMAKE_PREFIX_PATH})
   list(APPEND _prefix_path "${ForgeContract_PREFIX}")
   list(REMOVE_DUPLICATES _prefix_path)
   string(REPLACE ";" "|" _prefix_path "${_prefix_path}")

   ExternalProject_Add(
      "${target}"
      SOURCE_DIR "${_source_dir}"
      BINARY_DIR "${_binary_dir}"
      DOWNLOAD_COMMAND ""
      UPDATE_COMMAND ""
      PATCH_COMMAND ""
      INSTALL_COMMAND ""
      BUILD_ALWAYS TRUE
      CMAKE_GENERATOR "${CMAKE_GENERATOR}"
      LIST_SEPARATOR "|"
      CMAKE_ARGS
         "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
         "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${ForgeContract_TOOLCHAIN}"
         "-DForgeContract_DIR:PATH=${CMAKE_CURRENT_FUNCTION_LIST_DIR}"
         "-DCMAKE_PREFIX_PATH:PATH=${_prefix_path}"
         "-DFORGE_CONTRACT_ARTIFACT_DIR:PATH=${_artifact_dir}"
      BUILD_COMMAND
         "${CMAKE_COMMAND}" --build <BINARY_DIR>
         --config "$<CONFIG>"
         --target "${ARG_CONTRACT}_artifacts" --parallel 4
      BUILD_BYPRODUCTS
         "${_artifact_dir}/${ARG_CONTRACT}.wasm"
         "${_artifact_dir}/${ARG_CONTRACT}.abi"
         "${_artifact_dir}/${ARG_CONTRACT}.contract.json"
   )
   set_target_properties(
      "${target}"
      PROPERTIES
         FORGE_CONTRACT_WASM_FILE "${_artifact_dir}/${ARG_CONTRACT}.wasm"
         FORGE_CONTRACT_ABI_FILE "${_artifact_dir}/${ARG_CONTRACT}.abi"
         FORGE_CONTRACT_MANIFEST_FILE "${_artifact_dir}/${ARG_CONTRACT}.contract.json"
   )
endfunction()

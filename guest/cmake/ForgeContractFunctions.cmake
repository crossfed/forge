include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/ForgeContractGraph.cmake")

function(forge_add_contract target)
   cmake_parse_arguments(
      ARG
      ""
      "CONTRACT;SOURCE_ROOT;DISPATCH_SOURCE;RICARDIAN_CONTRACTS;RICARDIAN_CLAUSES"
      "SOURCES;HEADERS;COMPILE_CHECKS;LIBRARIES"
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "forge_add_contract(${target}) received unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
   endif()
   if(NOT ARG_SOURCES)
      message(FATAL_ERROR "forge_add_contract(${target}) requires SOURCES")
   endif()
   if(TARGET ${target})
      message(FATAL_ERROR "forge_add_contract target already exists: ${target}")
   endif()
   if(NOT ARG_CONTRACT)
      set(ARG_CONTRACT "${target}")
   endif()
   if(ARG_SOURCE_ROOT)
      _forge_contract_normalize_root("${ARG_SOURCE_ROOT}" "contract SOURCE_ROOT" _source_root)
   else()
      _forge_contract_normalize_root("${CMAKE_CURRENT_SOURCE_DIR}" "contract SOURCE_ROOT" _source_root)
   endif()

   set(_all_logical)
   set(_sources)
   set(_source_logical)
   foreach(_source IN LISTS ARG_SOURCES)
      _forge_contract_normalize_file(
         "${_source_root}" "${_source}" "contract source" _absolute _logical
      )
      if(_logical IN_LIST _all_logical)
         message(FATAL_ERROR "contract input is declared more than once: ${_logical}")
      endif()
      list(APPEND _all_logical "${_logical}")
      list(APPEND _sources "${_absolute}")
      list(APPEND _source_logical "contract/source/${_logical}")
   endforeach()

   list(LENGTH _sources _source_count)
   if(NOT ARG_DISPATCH_SOURCE)
      if(_source_count GREATER 1)
         message(FATAL_ERROR "forge_add_contract(${target}) requires DISPATCH_SOURCE when SOURCES has multiple files")
      endif()
      list(GET _sources 0 _dispatch_source)
   else()
      _forge_contract_normalize_file(
         "${_source_root}" "${ARG_DISPATCH_SOURCE}" "contract dispatch source"
         _dispatch_source _unused_logical
      )
      list(FIND _sources "${_dispatch_source}" _dispatch_source_index)
      if(_dispatch_source_index EQUAL -1)
         message(FATAL_ERROR "forge_add_contract(${target}) DISPATCH_SOURCE must also be listed in SOURCES")
      endif()
   endif()

   set(_headers)
   set(_header_logical)
   foreach(_header IN LISTS ARG_HEADERS)
      _forge_contract_normalize_file(
         "${_source_root}" "${_header}" "contract header" _absolute _logical
      )
      if(_logical IN_LIST _all_logical)
         message(FATAL_ERROR "contract input is declared more than once: ${_logical}")
      endif()
      list(APPEND _all_logical "${_logical}")
      list(APPEND _headers "${_absolute}")
      list(APPEND _header_logical "contract/header/${_logical}")
   endforeach()
   set(_compile_checks)
   set(_compile_check_logical)
   foreach(_source IN LISTS ARG_COMPILE_CHECKS)
      _forge_contract_normalize_file(
         "${_source_root}" "${_source}" "contract compile-check source" _absolute _logical
      )
      if(_logical IN_LIST _all_logical)
         message(FATAL_ERROR "contract input is declared more than once: ${_logical}")
      endif()
      list(APPEND _all_logical "${_logical}")
      list(APPEND _compile_checks "${_absolute}")
      list(APPEND _compile_check_logical "contract/compile-check/${_logical}")
   endforeach()
   set(_ricardian_contracts "")
   set(_ricardian_contracts_logical "")
   if(ARG_RICARDIAN_CONTRACTS)
      _forge_contract_normalize_file(
         "${_source_root}" "${ARG_RICARDIAN_CONTRACTS}" "Ricardian contracts file"
         _ricardian_contracts _ricardian_contracts_logical
      )
   endif()

   set(_ricardian_clauses "")
   set(_ricardian_clauses_logical "")
   if(ARG_RICARDIAN_CLAUSES)
      _forge_contract_normalize_file(
         "${_source_root}" "${ARG_RICARDIAN_CLAUSES}" "Ricardian clauses file"
         _ricardian_clauses _ricardian_clauses_logical
      )
   endif()

   set(_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract")
   _forge_contract_write_graph(
      TARGET "${target}"
      CONTRACT "${ARG_CONTRACT}"
      SOURCE_ROOT "${_source_root}"
      DISPATCH_SOURCE "${_dispatch_source}"
      SOURCES ${_sources}
      SOURCE_LOGICAL ${_source_logical}
      HEADERS ${_headers}
      HEADER_LOGICAL ${_header_logical}
      COMPILE_CHECKS ${_compile_checks}
      COMPILE_CHECK_LOGICAL ${_compile_check_logical}
      RICARDIAN_CONTRACTS "${_ricardian_contracts}"
      RICARDIAN_CONTRACTS_LOGICAL "${_ricardian_contracts_logical}"
      RICARDIAN_CLAUSES "${_ricardian_clauses}"
      RICARDIAN_CLAUSES_LOGICAL "${_ricardian_clauses_logical}"
      LIBRARIES ${ARG_LIBRARIES}
      OUTPUT_FILE _graph_file
      OUTPUT_HASH _graph_hash
      BUILD_DEPENDENCIES _build_dependencies
   )
   ExternalProject_Add(
      ${target}
      SOURCE_DIR "${ForgeContract_DATA_DIR}/build"
      BINARY_DIR "${_binary_dir}"
      DOWNLOAD_COMMAND ""
      UPDATE_COMMAND ""
      PATCH_COMMAND ""
      INSTALL_COMMAND ""
      BUILD_ALWAYS TRUE
      DEPENDS ${_build_dependencies}
      CMAKE_GENERATOR "${CMAKE_GENERATOR}"
      CMAKE_ARGS
         -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
         -DCMAKE_TOOLCHAIN_FILE=${ForgeContract_TOOLCHAIN}
         -DFORGE_CONTRACT_SDK_PREFIX:PATH=${ForgeContract_PREFIX}
         -DFORGE_CONTRACT_TARGET=${target}
         -DFORGE_CONTRACT_GRAPH_FILE=${_graph_file}
         -DFORGE_CONTRACT_GRAPH_HASH=${_graph_hash}
         -DFORGE_CONTRACT_COMPONENTS=${ForgeContract_COMPONENTS}
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
      BUILD_BYPRODUCTS
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.wasm"
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.abi"
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract.json"
   )
endfunction()

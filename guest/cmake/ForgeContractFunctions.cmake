include(ExternalProject)
include(GNUInstallDirs)

set(
   _FORGE_CONTRACT_EXPORTED_PROPERTIES
   FORGE_CONTRACT_LIBRARY
   FORGE_CONTRACT_LIBRARY_ID
   FORGE_CONTRACT_MODULE_BASE_DIRS
   FORGE_CONTRACT_MODULE_SOURCES
   FORGE_CONTRACT_SOURCES
   FORGE_CONTRACT_PUBLIC_HEADERS
   FORGE_CONTRACT_PRIVATE_HEADERS
   FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE
   FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE
   FORGE_CONTRACT_INSTALL_MODULE_PATHS
   FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS
)

function(_forge_contract_reject_unsafe_value value description)
   string(FIND "${value}" "|" _pipe_index)
   string(FIND "${value}" ";" _list_index)
   string(FIND "${value}" "\r" _carriage_return_index)
   string(FIND "${value}" "\n" _newline_index)
   string(FIND "${value}" "]==]" _bracket_index)
   if(
      NOT _pipe_index EQUAL -1
      OR NOT _list_index EQUAL -1
      OR NOT _carriage_return_index EQUAL -1
      OR NOT _newline_index EQUAL -1
      OR NOT _bracket_index EQUAL -1
   )
      message(FATAL_ERROR "${description} contains a reserved Contract SDK separator: ${value}")
   endif()
endfunction()

function(_forge_contract_normalize_file root input description output_absolute output_logical)
   get_filename_component(_absolute "${input}" REALPATH BASE_DIR "${root}")
   if(NOT EXISTS "${_absolute}" OR IS_DIRECTORY "${_absolute}")
      message(FATAL_ERROR "${description} does not exist or is not a file: ${_absolute}")
   endif()
   file(RELATIVE_PATH _logical "${root}" "${_absolute}")
   if(IS_ABSOLUTE "${_logical}" OR _logical MATCHES "^\\.\\.(/|$)")
      message(FATAL_ERROR "${description} is outside SOURCE_ROOT: ${_absolute}")
   endif()
   _forge_contract_reject_unsafe_value("${_absolute}" "${description}")
   _forge_contract_reject_unsafe_value("${_logical}" "${description}")
   set(${output_absolute} "${_absolute}" PARENT_SCOPE)
   set(${output_logical} "${_logical}" PARENT_SCOPE)
endfunction()

function(_forge_contract_normalize_directory root input description output_absolute output_logical)
   get_filename_component(_absolute "${input}" REALPATH BASE_DIR "${root}")
   if(NOT IS_DIRECTORY "${_absolute}")
      message(FATAL_ERROR "${description} is not a directory: ${_absolute}")
   endif()
   file(RELATIVE_PATH _logical "${root}" "${_absolute}")
   if(IS_ABSOLUTE "${_logical}" OR _logical MATCHES "^\\.\\.(/|$)")
      message(FATAL_ERROR "${description} is outside SOURCE_ROOT: ${_absolute}")
   endif()
   _forge_contract_reject_unsafe_value("${_absolute}" "${description}")
   _forge_contract_reject_unsafe_value("${_logical}" "${description}")
   set(${output_absolute} "${_absolute}" PARENT_SCOPE)
   set(${output_logical} "${_logical}" PARENT_SCOPE)
endfunction()

function(_forge_contract_path_under_base path bases description output)
   set(_match "")
   foreach(_base IN LISTS bases)
      file(RELATIVE_PATH _relative "${_base}" "${path}")
      if(NOT IS_ABSOLUTE "${_relative}" AND NOT _relative MATCHES "^\\.\\.(/|$)")
         set(_match "${_relative}")
         break()
      endif()
   endforeach()
   if(_match STREQUAL "")
      message(FATAL_ERROR "${description} is not contained by MODULE_BASE_DIRS: ${path}")
   endif()
   set(${output} "${_match}" PARENT_SCOPE)
endfunction()

function(_forge_contract_canonical_target input output)
   if(NOT TARGET "${input}")
      message(FATAL_ERROR "unknown Contract SDK library target: ${input}")
   endif()
   get_target_property(_aliased "${input}" ALIASED_TARGET)
   if(_aliased)
      set(${output} "${_aliased}" PARENT_SCOPE)
   else()
      set(${output} "${input}" PARENT_SCOPE)
   endif()
endfunction()

function(_forge_contract_guest_dependency input output)
   set(_name "${input}")
   if(_name MATCHES "^Forge::(.+)$")
      set(_name "${CMAKE_MATCH_1}")
   endif()

   if(_name STREQUAL "forge_raw")
      set(_guest forge_guest_raw)
   elseif(_name STREQUAL "forge_codec_base64")
      set(_guest forge_guest_codec_base64)
   elseif(_name STREQUAL "forge_codec_base58")
      set(_guest forge_guest_codec_base58)
   elseif(_name STREQUAL "forge_codec_hex")
      set(_guest forge_guest_codec_hex)
   elseif(
      _name STREQUAL "forge_crypto_digest"
      OR _name STREQUAL "forge_crypto_asymmetric_values"
      OR _name STREQUAL "forge_crypto_bls_values"
   )
      set(_guest forge_guest_crypto)
   elseif(_name STREQUAL "forge_chain_protocol")
      set(_guest forge_guest_chain_protocol)
   else()
      set(_guest "")
   endif()
   set(${output} "${_guest}" PARENT_SCOPE)
endfunction()

function(forge_add_contract_library target)
   cmake_parse_arguments(
      ARG
      ""
      "ID;SOURCE_ROOT"
      "MODULE_BASE_DIRS;MODULE_SOURCES;SOURCES;PUBLIC_HEADERS;PRIVATE_HEADERS;LIBRARIES"
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "forge_add_contract_library(${target}) received unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
   endif()
   if(TARGET "${target}")
      message(FATAL_ERROR "forge_add_contract_library target already exists: ${target}")
   endif()
   if(NOT ARG_ID)
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires ID")
   endif()
   if(NOT ARG_ID MATCHES "^[A-Za-z0-9][A-Za-z0-9_.-]*$")
      message(FATAL_ERROR "forge_add_contract_library(${target}) ID is not canonical: ${ARG_ID}")
   endif()
   if(NOT ARG_SOURCE_ROOT)
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires SOURCE_ROOT")
   endif()
   if(NOT ARG_MODULE_BASE_DIRS)
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires MODULE_BASE_DIRS")
   endif()
   if(NOT ARG_MODULE_SOURCES)
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires MODULE_SOURCES")
   endif()

   get_filename_component(_source_root "${ARG_SOURCE_ROOT}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
   if(NOT IS_DIRECTORY "${_source_root}")
      message(FATAL_ERROR "forge_add_contract_library(${target}) SOURCE_ROOT is not a directory: ${_source_root}")
   endif()

   get_property(_registered_ids GLOBAL PROPERTY FORGE_CONTRACT_LIBRARY_IDS)
   if(ARG_ID IN_LIST _registered_ids)
      message(FATAL_ERROR "duplicate Forge Contract library ID: ${ARG_ID}")
   endif()
   set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_LIBRARY_IDS "${ARG_ID}")

   set(_module_base_directories)
   set(_module_base_logical)
   foreach(_base IN LISTS ARG_MODULE_BASE_DIRS)
      _forge_contract_normalize_directory(
         "${_source_root}" "${_base}" "contract module base directory"
         _absolute _logical
      )
      if(_logical IN_LIST _module_base_logical)
         message(FATAL_ERROR "duplicate contract module base directory: ${_logical}")
      endif()
      list(APPEND _module_base_directories "${_absolute}")
      list(APPEND _module_base_logical "${_logical}")
   endforeach()

   set(_all_logical)
   foreach(_role MODULE_SOURCES SOURCES PUBLIC_HEADERS PRIVATE_HEADERS)
      set(_absolute_files)
      set(_logical_files)
      foreach(_file IN LISTS ARG_${_role})
         _forge_contract_normalize_file(
            "${_source_root}" "${_file}" "contract ${_role} file"
            _absolute _logical
         )
         if(_logical IN_LIST _all_logical)
            message(FATAL_ERROR "contract source is declared more than once: ${_logical}")
         endif()
         list(APPEND _all_logical "${_logical}")
         list(APPEND _absolute_files "${_absolute}")
         list(APPEND _logical_files "${_logical}")
      endforeach()
      set(_${_role}_ABSOLUTE "${_absolute_files}")
      set(_${_role}_LOGICAL "${_logical_files}")
   endforeach()

   foreach(_file IN LISTS _MODULE_SOURCES_ABSOLUTE _PUBLIC_HEADERS_ABSOLUTE)
      _forge_contract_path_under_base(
         "${_file}" "${_module_base_directories}" "public contract source"
         _unused_relative
      )
   endforeach()

   foreach(_library IN LISTS ARG_LIBRARIES)
      _forge_contract_canonical_target("${_library}" _canonical)
      get_target_property(_contract_library "${_canonical}" FORGE_CONTRACT_LIBRARY)
      _forge_contract_guest_dependency("${_canonical}" _guest_dependency)
      if(NOT _contract_library AND _guest_dependency STREQUAL "")
         message(
            FATAL_ERROR
            "forge_add_contract_library(${target}) dependency is not guest-compatible: ${_library}"
         )
      endif()
   endforeach()

   add_library(${target} STATIC)
   target_sources(
      ${target}
      PUBLIC
         FILE_SET forge_contract_modules TYPE CXX_MODULES
         BASE_DIRS ${_module_base_directories}
         FILES ${_MODULE_SOURCES_ABSOLUTE}
   )
   if(_SOURCES_ABSOLUTE)
      target_sources(${target} PRIVATE ${_SOURCES_ABSOLUTE})
   endif()
   if(_PUBLIC_HEADERS_ABSOLUTE)
      target_sources(
         ${target}
         PUBLIC
            FILE_SET forge_contract_public_headers TYPE HEADERS
            BASE_DIRS ${_module_base_directories}
            FILES ${_PUBLIC_HEADERS_ABSOLUTE}
      )
   endif()
   if(_PRIVATE_HEADERS_ABSOLUTE)
      target_sources(${target} PRIVATE ${_PRIVATE_HEADERS_ABSOLUTE})
   endif()
   target_compile_features(${target} PUBLIC cxx_std_23)
   if(ARG_LIBRARIES)
      target_link_libraries(${target} PUBLIC ${ARG_LIBRARIES})
   endif()

   set_target_properties(
      ${target}
      PROPERTIES
         EXPORT_NO_SYSTEM TRUE
         NO_SYSTEM_FROM_IMPORTED TRUE
         FORGE_CONTRACT_LIBRARY TRUE
         FORGE_CONTRACT_LIBRARY_ID "${ARG_ID}"
         FORGE_CONTRACT_BUILD_SOURCE_ROOT "${_source_root}"
         FORGE_CONTRACT_MODULE_BASE_DIRS "${_module_base_logical}"
         FORGE_CONTRACT_MODULE_SOURCES "${_MODULE_SOURCES_LOGICAL}"
         FORGE_CONTRACT_SOURCES "${_SOURCES_LOGICAL}"
         FORGE_CONTRACT_PUBLIC_HEADERS "${_PUBLIC_HEADERS_LOGICAL}"
         FORGE_CONTRACT_PRIVATE_HEADERS "${_PRIVATE_HEADERS_LOGICAL}"
         FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE ""
         FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE ""
         FORGE_CONTRACT_INSTALL_MODULE_PATHS ""
         FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS ""
   )
endfunction()

function(forge_install_contract_library)
   cmake_parse_arguments(
      ARG
      ""
      "TARGET;EXPORT;MODULE_DESTINATION;SOURCE_DESTINATION"
      ""
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "forge_install_contract_library received unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
   endif()
   foreach(_required TARGET EXPORT MODULE_DESTINATION SOURCE_DESTINATION)
      if(NOT ARG_${_required})
         message(FATAL_ERROR "forge_install_contract_library requires ${_required}")
      endif()
   endforeach()
   _forge_contract_canonical_target("${ARG_TARGET}" _target)
   get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
   if(NOT _contract_library)
      message(FATAL_ERROR "forge_install_contract_library target is not a contract library: ${ARG_TARGET}")
   endif()
   if(IS_ABSOLUTE "${ARG_MODULE_DESTINATION}" OR IS_ABSOLUTE "${ARG_SOURCE_DESTINATION}")
      message(FATAL_ERROR "contract library install destinations must be relative")
   endif()

   get_target_property(_source_root "${_target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
   get_target_property(_module_bases "${_target}" FORGE_CONTRACT_MODULE_BASE_DIRS)
   get_target_property(_module_sources "${_target}" FORGE_CONTRACT_MODULE_SOURCES)
   get_target_property(_sources "${_target}" FORGE_CONTRACT_SOURCES)
   get_target_property(_public_headers "${_target}" FORGE_CONTRACT_PUBLIC_HEADERS)
   get_target_property(_private_headers "${_target}" FORGE_CONTRACT_PRIVATE_HEADERS)

   set(_absolute_module_bases)
   foreach(_base IN LISTS _module_bases)
      get_filename_component(_absolute "${_source_root}/${_base}" ABSOLUTE)
      list(APPEND _absolute_module_bases "${_absolute}")
   endforeach()

   set(_installed_module_paths)
   foreach(_logical IN LISTS _module_sources)
      get_filename_component(_absolute "${_source_root}/${_logical}" ABSOLUTE)
      _forge_contract_path_under_base(
         "${_absolute}" "${_absolute_module_bases}" "contract module"
         _installed
      )
      if(_installed IN_LIST _installed_module_paths)
         message(FATAL_ERROR "contract modules have the same installed logical path: ${_installed}")
      endif()
      list(APPEND _installed_module_paths "${_installed}")
   endforeach()

   set(_installed_public_header_paths)
   foreach(_logical IN LISTS _public_headers)
      get_filename_component(_absolute "${_source_root}/${_logical}" ABSOLUTE)
      _forge_contract_path_under_base(
         "${_absolute}" "${_absolute_module_bases}" "contract public header"
         _installed
      )
      if(_installed IN_LIST _installed_module_paths OR _installed IN_LIST _installed_public_header_paths)
         message(FATAL_ERROR "contract public sources have the same installed logical path: ${_installed}")
      endif()
      list(APPEND _installed_public_header_paths "${_installed}")
   endforeach()

   set(_prefix_anchor "/__forge_contract_prefix")
   file(
      RELATIVE_PATH _module_root_relative
      "${_prefix_anchor}/${CMAKE_INSTALL_LIBDIR}"
      "${_prefix_anchor}/${ARG_MODULE_DESTINATION}"
   )
   file(
      RELATIVE_PATH _source_root_relative
      "${_prefix_anchor}/${CMAKE_INSTALL_LIBDIR}"
      "${_prefix_anchor}/${ARG_SOURCE_DESTINATION}"
   )
   set_target_properties(
      ${_target}
      PROPERTIES
         FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE "${_module_root_relative}"
         FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE "${_source_root_relative}"
         FORGE_CONTRACT_INSTALL_MODULE_PATHS "${_installed_module_paths}"
         FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS "${_installed_public_header_paths}"
         EXPORT_PROPERTIES "${_FORGE_CONTRACT_EXPORTED_PROPERTIES}"
   )

   if(_public_headers)
      install(
         TARGETS ${_target}
         EXPORT ${ARG_EXPORT}
         LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
         ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
         RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
         FILE_SET forge_contract_modules DESTINATION "${ARG_MODULE_DESTINATION}"
         FILE_SET forge_contract_public_headers DESTINATION "${ARG_MODULE_DESTINATION}"
      )
   else()
      install(
         TARGETS ${_target}
         EXPORT ${ARG_EXPORT}
         LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
         ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
         RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
         FILE_SET forge_contract_modules DESTINATION "${ARG_MODULE_DESTINATION}"
      )
   endif()

   foreach(_logical IN LISTS _sources _private_headers)
      get_filename_component(_directory "${_logical}" DIRECTORY)
      if(_directory STREQUAL "")
         set(_destination "${ARG_SOURCE_DESTINATION}")
      else()
         set(_destination "${ARG_SOURCE_DESTINATION}/${_directory}")
      endif()
      install(FILES "${_source_root}/${_logical}" DESTINATION "${_destination}")
   endforeach()
endfunction()

function(_forge_contract_imported_location target output)
   get_target_property(_location "${target}" IMPORTED_LOCATION)
   if(NOT _location)
      get_target_property(_configurations "${target}" IMPORTED_CONFIGURATIONS)
      foreach(_configuration IN LISTS _configurations)
         string(TOUPPER "${_configuration}" _upper)
         get_target_property(_candidate "${target}" "IMPORTED_LOCATION_${_upper}")
         if(_candidate)
            set(_location "${_candidate}")
            break()
         endif()
      endforeach()
   endif()
   if(NOT _location)
      message(FATAL_ERROR "imported contract library has no archive location: ${target}")
   endif()
   set(${output} "${_location}" PARENT_SCOPE)
endfunction()

function(_forge_contract_target_dependencies target output)
   get_target_property(_dependencies "${target}" INTERFACE_LINK_LIBRARIES)
   if(NOT _dependencies)
      set(_dependencies)
   endif()
   set(${output} "${_dependencies}" PARENT_SCOPE)
endfunction()

function(_forge_contract_collect_library graph target)
   _forge_contract_canonical_target("${target}" _target)
   _forge_contract_guest_dependency("${_target}" _guest_dependency)
   if(NOT _guest_dependency STREQUAL "")
      return()
   endif()

   get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
   if(NOT _contract_library)
      message(FATAL_ERROR "contract dependency is not guest-compatible: ${target}")
   endif()

   string(SHA256 _target_hash "${_target}")
   get_property(_state GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_target_hash}_STATE")
   if(_state STREQUAL "visited")
      return()
   endif()
   if(_state STREQUAL "visiting")
      message(FATAL_ERROR "cycle in Forge Contract library dependencies at ${target}")
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_target_hash}_STATE" visiting)

   _forge_contract_target_dependencies("${_target}" _dependencies)
   foreach(_dependency IN LISTS _dependencies)
      if(_dependency MATCHES "\\$<")
         message(FATAL_ERROR "generator expressions are not supported in contract library dependencies: ${_dependency}")
      endif()
      _forge_contract_collect_library("${graph}" "${_dependency}")
   endforeach()

   get_property(_nodes GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_NODES")
   list(APPEND _nodes "${_target}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_NODES" "${_nodes}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_target_hash}_STATE" visited)
endfunction()

function(_forge_contract_append_set path name value)
   file(APPEND "${path}" "set(${name} [==[${value}]==])\n")
endfunction()

function(_forge_contract_write_graph target libraries output)
   string(SHA256 _graph "${CMAKE_CURRENT_BINARY_DIR}/${target}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph}_NODES" "")
   set(_root_guest_dependencies)
   set(_root_library_ids)
   foreach(_library IN LISTS libraries)
      _forge_contract_canonical_target("${_library}" _canonical)
      _forge_contract_guest_dependency("${_canonical}" _guest_dependency)
      if(NOT _guest_dependency STREQUAL "")
         list(APPEND _root_guest_dependencies "${_guest_dependency}")
      else()
         get_target_property(_library_id "${_canonical}" FORGE_CONTRACT_LIBRARY_ID)
         list(APPEND _root_library_ids "${_library_id}")
      endif()
      _forge_contract_collect_library("${_graph}" "${_library}")
   endforeach()
   list(REMOVE_DUPLICATES _root_guest_dependencies)
   list(REMOVE_DUPLICATES _root_library_ids)
   get_property(_nodes GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph}_NODES")

   set(_path "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract-graph.cmake")
   file(WRITE "${_path}" "set(FORGE_CONTRACT_GRAPH_VERSION 1)\n")
   _forge_contract_append_set(
      "${_path}"
      FORGE_CONTRACT_GRAPH_ROOT_GUEST_DEPENDENCIES
      "${_root_guest_dependencies}"
   )
   _forge_contract_append_set("${_path}" FORGE_CONTRACT_GRAPH_ROOT_LIBRARY_IDS "${_root_library_ids}")
   list(LENGTH _nodes _node_count)
   file(APPEND "${_path}" "set(FORGE_CONTRACT_GRAPH_LIBRARY_COUNT ${_node_count})\n")

   set(_ids)
   set(_index 0)
   foreach(_node IN LISTS _nodes)
      get_target_property(_id "${_node}" FORGE_CONTRACT_LIBRARY_ID)
      if(_id IN_LIST _ids)
         message(FATAL_ERROR "duplicate Forge Contract library ID in graph: ${_id}")
      endif()
      list(APPEND _ids "${_id}")

      get_target_property(_imported "${_node}" IMPORTED)
      get_target_property(_module_bases "${_node}" FORGE_CONTRACT_MODULE_BASE_DIRS)
      get_target_property(_module_sources "${_node}" FORGE_CONTRACT_MODULE_SOURCES)
      get_target_property(_sources "${_node}" FORGE_CONTRACT_SOURCES)
      get_target_property(_public_headers "${_node}" FORGE_CONTRACT_PUBLIC_HEADERS)
      get_target_property(_private_headers "${_node}" FORGE_CONTRACT_PRIVATE_HEADERS)

      if(_imported)
         _forge_contract_imported_location("${_node}" _archive)
         get_filename_component(_archive_directory "${_archive}" DIRECTORY)
         get_target_property(_module_root_relative "${_node}" FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE)
         get_target_property(_source_root_relative "${_node}" FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE)
         get_filename_component(_module_root "${_archive_directory}/${_module_root_relative}" ABSOLUTE)
         get_filename_component(_source_root "${_archive_directory}/${_source_root_relative}" ABSOLUTE)
         get_target_property(_installed_modules "${_node}" FORGE_CONTRACT_INSTALL_MODULE_PATHS)
         get_target_property(_installed_public_headers "${_node}" FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS)
         set(_module_base_paths "${_module_root}")
         set(_module_paths)
         foreach(_installed IN LISTS _installed_modules)
            list(APPEND _module_paths "${_module_root}/${_installed}")
         endforeach()
         set(_public_header_paths)
         foreach(_installed IN LISTS _installed_public_headers)
            list(APPEND _public_header_paths "${_module_root}/${_installed}")
         endforeach()
      else()
         get_target_property(_source_root "${_node}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
         set(_module_base_paths)
         foreach(_base IN LISTS _module_bases)
            list(APPEND _module_base_paths "${_source_root}/${_base}")
         endforeach()
         set(_module_paths)
         foreach(_logical IN LISTS _module_sources)
            list(APPEND _module_paths "${_source_root}/${_logical}")
         endforeach()
         set(_public_header_paths)
         foreach(_logical IN LISTS _public_headers)
            list(APPEND _public_header_paths "${_source_root}/${_logical}")
         endforeach()
      endif()

      set(_source_paths)
      foreach(_logical IN LISTS _sources)
         list(APPEND _source_paths "${_source_root}/${_logical}")
      endforeach()
      set(_private_header_paths)
      foreach(_logical IN LISTS _private_headers)
         list(APPEND _private_header_paths "${_source_root}/${_logical}")
      endforeach()

      set(_dependency_ids)
      set(_guest_dependencies)
      _forge_contract_target_dependencies("${_node}" _dependencies)
      foreach(_dependency IN LISTS _dependencies)
         _forge_contract_canonical_target("${_dependency}" _canonical)
         _forge_contract_guest_dependency("${_canonical}" _guest_dependency)
         if(NOT _guest_dependency STREQUAL "")
            list(APPEND _guest_dependencies "${_guest_dependency}")
         else()
            get_target_property(_dependency_id "${_canonical}" FORGE_CONTRACT_LIBRARY_ID)
            list(APPEND _dependency_ids "${_dependency_id}")
         endif()
      endforeach()
      list(REMOVE_DUPLICATES _guest_dependencies)

      set(_prefix "FORGE_CONTRACT_GRAPH_LIBRARY_${_index}")
      _forge_contract_append_set("${_path}" "${_prefix}_ID" "${_id}")
      _forge_contract_append_set("${_path}" "${_prefix}_MODULE_BASES" "${_module_base_paths}")
      _forge_contract_append_set("${_path}" "${_prefix}_MODULE_LOGICAL" "${_module_sources}")
      _forge_contract_append_set("${_path}" "${_prefix}_MODULE_PATHS" "${_module_paths}")
      _forge_contract_append_set("${_path}" "${_prefix}_SOURCE_LOGICAL" "${_sources}")
      _forge_contract_append_set("${_path}" "${_prefix}_SOURCE_PATHS" "${_source_paths}")
      _forge_contract_append_set("${_path}" "${_prefix}_PUBLIC_HEADER_LOGICAL" "${_public_headers}")
      _forge_contract_append_set("${_path}" "${_prefix}_PUBLIC_HEADER_PATHS" "${_public_header_paths}")
      _forge_contract_append_set("${_path}" "${_prefix}_PRIVATE_HEADER_LOGICAL" "${_private_headers}")
      _forge_contract_append_set("${_path}" "${_prefix}_PRIVATE_HEADER_PATHS" "${_private_header_paths}")
      _forge_contract_append_set("${_path}" "${_prefix}_DEPENDENCY_IDS" "${_dependency_ids}")
      _forge_contract_append_set("${_path}" "${_prefix}_GUEST_DEPENDENCIES" "${_guest_dependencies}")
      math(EXPR _index "${_index} + 1")
   endforeach()

   file(SHA256 "${_path}" _hash)
   set(${output} "${_path};${_hash}" PARENT_SCOPE)
endfunction()

function(forge_add_contract target)
   cmake_parse_arguments(
      ARG
      ""
      "CONTRACT;DISPATCH_SOURCE;RICARDIAN_CONTRACTS;RICARDIAN_CLAUSES"
      "SOURCES;COMPILE_CHECKS;INCLUDE_DIRECTORIES;LIBRARIES"
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

   set(_sources)
   foreach(_source IN LISTS ARG_SOURCES)
      get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
      if(NOT EXISTS "${_absolute}")
         message(FATAL_ERROR "contract source does not exist: ${_absolute}")
      endif()
      list(APPEND _sources "${_absolute}")
   endforeach()

   list(LENGTH _sources _source_count)
   if(NOT ARG_DISPATCH_SOURCE)
      if(_source_count GREATER 1)
         message(FATAL_ERROR "forge_add_contract(${target}) requires DISPATCH_SOURCE when SOURCES has multiple files")
      endif()
      list(GET _sources 0 _dispatch_source)
   else()
      get_filename_component(
         _dispatch_source
         "${ARG_DISPATCH_SOURCE}"
         ABSOLUTE
         BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
      )
      list(FIND _sources "${_dispatch_source}" _dispatch_source_index)
      if(_dispatch_source_index EQUAL -1)
         message(FATAL_ERROR "forge_add_contract(${target}) DISPATCH_SOURCE must also be listed in SOURCES")
      endif()
   endif()

   list(REMOVE_ITEM _sources "${_dispatch_source}")
   list(INSERT _sources 0 "${_dispatch_source}")
   string(JOIN "|" _encoded_sources ${_sources})

   set(_compile_checks)
   foreach(_source IN LISTS ARG_COMPILE_CHECKS)
      get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
      if(NOT EXISTS "${_absolute}")
         message(FATAL_ERROR "contract compile-check source does not exist: ${_absolute}")
      endif()
      list(APPEND _compile_checks "${_absolute}")
   endforeach()
   string(JOIN "|" _encoded_compile_checks ${_compile_checks})

   set(_include_directories)
   foreach(_include_directory IN LISTS ARG_INCLUDE_DIRECTORIES)
      get_filename_component(
         _absolute
         "${_include_directory}"
         REALPATH
         BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
      )
      if(NOT IS_DIRECTORY "${_absolute}")
         message(FATAL_ERROR "contract include directory does not exist: ${_absolute}")
      endif()
      list(APPEND _include_directories "${_absolute}")
   endforeach()
   string(JOIN "|" _encoded_include_directories ${_include_directories})

   set(_ricardian_contracts "")
   if(ARG_RICARDIAN_CONTRACTS)
      get_filename_component(
         _ricardian_contracts
         "${ARG_RICARDIAN_CONTRACTS}"
         REALPATH
         BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
      )
      if(NOT EXISTS "${_ricardian_contracts}")
         message(FATAL_ERROR "Ricardian contracts file does not exist: ${_ricardian_contracts}")
      endif()
   endif()

   set(_ricardian_clauses "")
   if(ARG_RICARDIAN_CLAUSES)
      get_filename_component(
         _ricardian_clauses
         "${ARG_RICARDIAN_CLAUSES}"
         REALPATH
         BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
      )
      if(NOT EXISTS "${_ricardian_clauses}")
         message(FATAL_ERROR "Ricardian clauses file does not exist: ${_ricardian_clauses}")
      endif()
   endif()

   _forge_contract_write_graph("${target}" "${ARG_LIBRARIES}" _graph)
   list(GET _graph 0 _graph_file)
   list(GET _graph 1 _graph_hash)

   set(_build_dependencies)
   foreach(_library IN LISTS ARG_LIBRARIES)
      _forge_contract_canonical_target("${_library}" _canonical)
      get_target_property(_imported "${_canonical}" IMPORTED)
      if(NOT _imported)
         list(APPEND _build_dependencies "${_canonical}")
      endif()
   endforeach()

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
      DEPENDS ${_build_dependencies}
      CMAKE_GENERATOR "${CMAKE_GENERATOR}"
      CMAKE_ARGS
         -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
         -DCMAKE_TOOLCHAIN_FILE=${ForgeContract_TOOLCHAIN}
         -DFORGE_CONTRACT_SDK_PREFIX:PATH=${ForgeContract_PREFIX}
         -DFORGE_CONTRACT_TARGET=${target}
         -DFORGE_CONTRACT_NAME=${ARG_CONTRACT}
         -DFORGE_CONTRACT_SOURCES_ENCODED=${_encoded_sources}
         -DFORGE_CONTRACT_COMPILE_CHECKS_ENCODED=${_encoded_compile_checks}
         -DFORGE_CONTRACT_INCLUDE_DIRECTORIES_ENCODED=${_encoded_include_directories}
         -DFORGE_CONTRACT_GRAPH_FILE=${_graph_file}
         -DFORGE_CONTRACT_GRAPH_HASH=${_graph_hash}
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
         -DFORGE_CONTRACT_RICARDIAN_CONTRACTS=${_ricardian_contracts}
         -DFORGE_CONTRACT_RICARDIAN_CLAUSES=${_ricardian_clauses}
      BUILD_BYPRODUCTS
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.wasm"
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.abi"
         "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract.json"
   )
endfunction()

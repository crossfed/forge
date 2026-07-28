include(GNUInstallDirs)

set(
   _FORGE_CONTRACT_EXPORTED_PROPERTIES
   FORGE_CONTRACT_DESCRIPTOR_SCHEMA
   FORGE_CONTRACT_LIBRARY
   FORGE_CONTRACT_LIBRARY_ID
   FORGE_CONTRACT_MODULE_BASE_DIRS
   FORGE_CONTRACT_MODULE_SOURCES
   FORGE_CONTRACT_SOURCES
   FORGE_CONTRACT_PUBLIC_HEADERS
   FORGE_CONTRACT_PRIVATE_HEADERS
   FORGE_CONTRACT_PUBLIC_LIBRARY_IDS
   FORGE_CONTRACT_PRIVATE_LIBRARY_IDS
   FORGE_CONTRACT_PUBLIC_COMPONENT_IDS
   FORGE_CONTRACT_PRIVATE_COMPONENT_IDS
   FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE
   FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE
   FORGE_CONTRACT_INSTALL_MODULE_PATHS
   FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS
)

function(_forge_contract_resolve_target input output)
   if(NOT TARGET "${input}")
      message(FATAL_ERROR "unknown Contract SDK dependency target: ${input}")
   endif()
   get_target_property(_aliased "${input}" ALIASED_TARGET)
   if(_aliased)
      set(${output} "${_aliased}" PARENT_SCOPE)
   else()
      set(${output} "${input}" PARENT_SCOPE)
   endif()
endfunction()

function(_forge_contract_reject_unsafe_value value description)
   string(FIND "${value}" ";" _semicolon)
   string(FIND "${value}" "\r" _carriage_return)
   string(FIND "${value}" "\n" _newline)
   if(NOT _semicolon EQUAL -1 OR NOT _carriage_return EQUAL -1 OR NOT _newline EQUAL -1)
      message(FATAL_ERROR "${description} contains a reserved descriptor character: ${value}")
   endif()
endfunction()

function(_forge_contract_normalize_root input description output)
   get_filename_component(_absolute "${input}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
   if(NOT IS_DIRECTORY "${_absolute}")
      message(FATAL_ERROR "${description} is not a directory: ${_absolute}")
   endif()
   _forge_contract_reject_unsafe_value("${_absolute}" "${description}")
   set(${output} "${_absolute}" PARENT_SCOPE)
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

function(_forge_contract_id_key id output)
   string(SHA256 _key "${id}")
   set(${output} "${_key}" PARENT_SCOPE)
endfunction()

function(_forge_contract_register_library_target target)
   get_target_property(_id "${target}" FORGE_CONTRACT_LIBRARY_ID)
   if(NOT _id)
      message(FATAL_ERROR "contract library target has no stable ID: ${target}")
   endif()
   _forge_contract_id_key("${_id}" _key)
   get_property(_registered GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}")
   if(_registered AND NOT _registered STREQUAL target)
      get_target_property(_registered_id "${_registered}" FORGE_CONTRACT_LIBRARY_ID)
      if(_registered_id STREQUAL _id)
         message(FATAL_ERROR "duplicate Forge Contract library ID: ${_id}")
      endif()
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}" "${target}")
endfunction()

function(_forge_contract_register_visible_imported_libraries)
   get_property(_imported DIRECTORY PROPERTY IMPORTED_TARGETS)
   foreach(_target IN LISTS _imported)
      get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
      if(_contract_library)
         _forge_contract_register_library_target("${_target}")
      endif()
   endforeach()
endfunction()

function(_forge_contract_find_library_target id output)
   _forge_contract_id_key("${id}" _key)
   get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}")
   if(NOT _target)
      _forge_contract_register_visible_imported_libraries()
      get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}")
   endif()
   if(NOT _target)
      message(FATAL_ERROR "contract library dependency ID is not visible: ${id}")
   endif()
   set(${output} "${_target}" PARENT_SCOPE)
endfunction()

function(_forge_contract_classify_dependency input kind_output id_output target_output)
   _forge_contract_resolve_target("${input}" _target)
   get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
   get_target_property(_component_id "${_target}" FORGE_CONTRACT_GUEST_COMPONENT_ID)
   if(_contract_library)
      get_target_property(_id "${_target}" FORGE_CONTRACT_LIBRARY_ID)
      set(_kind library)
      _forge_contract_register_library_target("${_target}")
   elseif(_component_id)
      set(_id "${_component_id}")
      set(_kind component)
   else()
      message(FATAL_ERROR "contract dependency is not guest-compatible: ${input}")
   endif()
   set(${kind_output} "${_kind}" PARENT_SCOPE)
   set(${id_output} "${_id}" PARENT_SCOPE)
   set(${target_output} "${_target}" PARENT_SCOPE)
endfunction()

function(_forge_contract_dependency_ids dependencies scope library_ids component_ids host_targets)
   set(_library_ids)
   set(_component_ids)
   set(_host_targets)
   foreach(_dependency IN LISTS dependencies)
      _forge_contract_classify_dependency("${_dependency}" _kind _id _target)
      if(_kind STREQUAL "library")
         if(_id IN_LIST _library_ids)
            message(FATAL_ERROR "duplicate ${scope} contract-library dependency: ${_id}")
         endif()
         list(APPEND _library_ids "${_id}")
      else()
         if(_id IN_LIST _component_ids)
            message(FATAL_ERROR "duplicate ${scope} Forge guest component dependency: ${_id}")
         endif()
         list(APPEND _component_ids "${_id}")
      endif()
      list(APPEND _host_targets "${_dependency}")
   endforeach()
   set(${library_ids} "${_library_ids}" PARENT_SCOPE)
   set(${component_ids} "${_component_ids}" PARENT_SCOPE)
   set(${host_targets} "${_host_targets}" PARENT_SCOPE)
endfunction()

function(forge_add_contract_library target)
   cmake_parse_arguments(
      ARG
      ""
      "ID;SOURCE_ROOT"
      "MODULE_BASE_DIRS;MODULE_SOURCES;SOURCES;PUBLIC_HEADERS;PRIVATE_HEADERS;PUBLIC_LIBRARIES;PRIVATE_LIBRARIES"
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "forge_add_contract_library(${target}) received unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
   endif()
   if(TARGET "${target}")
      message(FATAL_ERROR "forge_add_contract_library target already exists: ${target}")
   endif()
   if(NOT ARG_ID OR NOT ARG_ID MATCHES "^[A-Za-z0-9][A-Za-z0-9_.-]*$")
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires a canonical ID")
   endif()
   if(NOT ARG_SOURCE_ROOT)
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires SOURCE_ROOT")
   endif()
   if(NOT ARG_MODULE_BASE_DIRS OR NOT ARG_MODULE_SOURCES)
      message(FATAL_ERROR "forge_add_contract_library(${target}) requires MODULE_BASE_DIRS and MODULE_SOURCES")
   endif()

   _forge_contract_normalize_root("${ARG_SOURCE_ROOT}" "contract SOURCE_ROOT" _source_root)

   _forge_contract_id_key("${ARG_ID}" _id_key)
   get_property(_existing GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_id_key}")
   if(_existing)
      message(FATAL_ERROR "duplicate Forge Contract library ID: ${ARG_ID}")
   endif()

   set(_module_bases_absolute)
   set(_module_bases_logical)
   foreach(_base IN LISTS ARG_MODULE_BASE_DIRS)
      _forge_contract_normalize_directory(
         "${_source_root}" "${_base}" "contract module base directory"
         _absolute _logical
      )
      if(_logical IN_LIST _module_bases_logical)
         message(FATAL_ERROR "duplicate contract module base directory: ${_logical}")
      endif()
      list(APPEND _module_bases_absolute "${_absolute}")
      list(APPEND _module_bases_logical "${_logical}")
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
            message(FATAL_ERROR "contract file is declared more than once: ${_logical}")
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
         "${_file}" "${_module_bases_absolute}" "public contract input" _unused
      )
   endforeach()

   _forge_contract_dependency_ids(
      "${ARG_PUBLIC_LIBRARIES}" PUBLIC
      _public_library_ids _public_component_ids _public_targets
   )
   _forge_contract_dependency_ids(
      "${ARG_PRIVATE_LIBRARIES}" PRIVATE
      _private_library_ids _private_component_ids _private_targets
   )
   foreach(_id IN LISTS _private_library_ids)
      if(_id IN_LIST _public_library_ids)
         message(FATAL_ERROR "contract library dependency has conflicting scopes: ${_id}")
      endif()
   endforeach()
   foreach(_id IN LISTS _private_component_ids)
      if(_id IN_LIST _public_component_ids)
         message(FATAL_ERROR "Forge guest component dependency has conflicting scopes: ${_id}")
      endif()
   endforeach()

   string(SUBSTRING "${_id_key}" 0 12 _short_id)
   set(_concrete "_forge_contract_library_${target}_${_short_id}")
   add_library("${_concrete}" STATIC)
   add_library("${target}" ALIAS "${_concrete}")
   set_target_properties(
      "${_concrete}"
      PROPERTIES
         OUTPUT_NAME "${target}"
         EXPORT_NAME "${target}"
         EXPORT_NO_SYSTEM TRUE
         NO_SYSTEM_FROM_IMPORTED TRUE
   )
   target_sources(
      "${_concrete}"
      PUBLIC
         FILE_SET forge_contract_modules TYPE CXX_MODULES
         BASE_DIRS ${_module_bases_absolute}
         FILES ${_MODULE_SOURCES_ABSOLUTE}
   )
   if(_SOURCES_ABSOLUTE OR _PRIVATE_HEADERS_ABSOLUTE)
      target_sources("${_concrete}" PRIVATE ${_SOURCES_ABSOLUTE} ${_PRIVATE_HEADERS_ABSOLUTE})
   endif()
   if(_PUBLIC_HEADERS_ABSOLUTE)
      target_sources(
         "${_concrete}"
         PUBLIC
            FILE_SET forge_contract_public_headers TYPE HEADERS
            BASE_DIRS ${_module_bases_absolute}
            FILES ${_PUBLIC_HEADERS_ABSOLUTE}
      )
   endif()
   target_compile_features("${_concrete}" PUBLIC cxx_std_23)
   if(_public_targets)
      target_link_libraries("${_concrete}" PUBLIC ${_public_targets})
   endif()
   if(_private_targets)
      target_link_libraries("${_concrete}" PRIVATE ${_private_targets})
   endif()

   set_target_properties(
      "${_concrete}"
      PROPERTIES
         FORGE_CONTRACT_DESCRIPTOR_SCHEMA 2
         FORGE_CONTRACT_LIBRARY TRUE
         FORGE_CONTRACT_LIBRARY_ID "${ARG_ID}"
         FORGE_CONTRACT_BUILD_SOURCE_ROOT "${_source_root}"
         FORGE_CONTRACT_MODULE_BASE_DIRS "${_module_bases_logical}"
         FORGE_CONTRACT_MODULE_SOURCES "${_MODULE_SOURCES_LOGICAL}"
         FORGE_CONTRACT_SOURCES "${_SOURCES_LOGICAL}"
         FORGE_CONTRACT_PUBLIC_HEADERS "${_PUBLIC_HEADERS_LOGICAL}"
         FORGE_CONTRACT_PRIVATE_HEADERS "${_PRIVATE_HEADERS_LOGICAL}"
         FORGE_CONTRACT_PUBLIC_LIBRARY_IDS "${_public_library_ids}"
         FORGE_CONTRACT_PRIVATE_LIBRARY_IDS "${_private_library_ids}"
         FORGE_CONTRACT_PUBLIC_COMPONENT_IDS "${_public_component_ids}"
         FORGE_CONTRACT_PRIVATE_COMPONENT_IDS "${_private_component_ids}"
         FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE ""
         FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE ""
         FORGE_CONTRACT_INSTALL_MODULE_PATHS ""
         FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS ""
   )
   _forge_contract_register_library_target("${_concrete}")
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

function(forge_install_contract_library)
   cmake_parse_arguments(
      ARG
      ""
      "TARGET;EXPORT;EXPORT_NAME;MODULE_DESTINATION;SOURCE_DESTINATION"
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
   if(IS_ABSOLUTE "${ARG_MODULE_DESTINATION}" OR IS_ABSOLUTE "${ARG_SOURCE_DESTINATION}")
      message(FATAL_ERROR "contract library install destinations must be relative")
   endif()

   _forge_contract_resolve_target("${ARG_TARGET}" _target)
   get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
   if(NOT _contract_library)
      message(FATAL_ERROR "forge_install_contract_library target is not a contract library: ${ARG_TARGET}")
   endif()
   if(ARG_EXPORT_NAME)
      set_target_properties("${_target}" PROPERTIES EXPORT_NAME "${ARG_EXPORT_NAME}")
   endif()

   get_target_property(_source_root "${_target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
   get_target_property(_module_bases "${_target}" FORGE_CONTRACT_MODULE_BASE_DIRS)
   get_target_property(_module_sources "${_target}" FORGE_CONTRACT_MODULE_SOURCES)
   get_target_property(_sources "${_target}" FORGE_CONTRACT_SOURCES)
   get_target_property(_public_headers "${_target}" FORGE_CONTRACT_PUBLIC_HEADERS)
   get_target_property(_private_headers "${_target}" FORGE_CONTRACT_PRIVATE_HEADERS)

   set(_absolute_module_bases)
   foreach(_base IN LISTS _module_bases)
      list(APPEND _absolute_module_bases "${_source_root}/${_base}")
   endforeach()
   set(_installed_modules)
   foreach(_logical IN LISTS _module_sources)
      _forge_contract_path_under_base(
         "${_source_root}/${_logical}" "${_absolute_module_bases}" "contract module" _installed
      )
      if(_installed IN_LIST _installed_modules)
         message(FATAL_ERROR "contract modules have the same installed path: ${_installed}")
      endif()
      list(APPEND _installed_modules "${_installed}")
   endforeach()
   set(_installed_public_headers)
   foreach(_logical IN LISTS _public_headers)
      _forge_contract_path_under_base(
         "${_source_root}/${_logical}" "${_absolute_module_bases}" "contract public header" _installed
      )
      if(_installed IN_LIST _installed_modules OR _installed IN_LIST _installed_public_headers)
         message(FATAL_ERROR "contract public inputs have the same installed path: ${_installed}")
      endif()
      list(APPEND _installed_public_headers "${_installed}")
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
      "${_target}"
      PROPERTIES
         FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE "${_module_root_relative}"
         FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE "${_source_root_relative}"
         FORGE_CONTRACT_INSTALL_MODULE_PATHS "${_installed_modules}"
         FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS "${_installed_public_headers}"
         EXPORT_PROPERTIES "${_FORGE_CONTRACT_EXPORTED_PROPERTIES}"
   )

   set(_file_sets FILE_SET forge_contract_modules DESTINATION "${ARG_MODULE_DESTINATION}")
   if(_public_headers)
      list(APPEND _file_sets FILE_SET forge_contract_public_headers DESTINATION "${ARG_MODULE_DESTINATION}")
   endif()
   install(
      TARGETS "${_target}"
      EXPORT "${ARG_EXPORT}"
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
      ${_file_sets}
   )
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

function(_forge_contract_json_quote value output)
   string(REPLACE "\\" "\\\\" _escaped "${value}")
   string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
   string(REPLACE "\t" "\\t" _escaped "${_escaped}")
   string(REPLACE "\r" "\\r" _escaped "${_escaped}")
   string(REPLACE "\n" "\\n" _escaped "${_escaped}")
   set(${output} "\"${_escaped}\"" PARENT_SCOPE)
endfunction()

function(_forge_contract_json_array values output)
   set(_array "[]")
   set(_index 0)
   foreach(_value IN LISTS values)
      _forge_contract_json_quote("${_value}" _quoted)
      string(JSON _array SET "${_array}" ${_index} "${_quoted}")
      math(EXPR _index "${_index} + 1")
   endforeach()
   set(${output} "${_array}" PARENT_SCOPE)
endfunction()

function(_forge_contract_collect_library graph id)
   _forge_contract_id_key("${id}" _key)
   get_property(_state GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_key}_STATE")
   if(_state STREQUAL "visited")
      return()
   endif()
   if(_state STREQUAL "visiting")
      message(FATAL_ERROR "cycle in Forge Contract library dependencies at ${id}")
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_key}_STATE" visiting)
   _forge_contract_find_library_target("${id}" _target)
   foreach(_property FORGE_CONTRACT_PUBLIC_LIBRARY_IDS FORGE_CONTRACT_PRIVATE_LIBRARY_IDS)
      get_target_property(_dependencies "${_target}" "${_property}")
      if(_dependencies STREQUAL "_dependencies-NOTFOUND")
         set(_dependencies)
      endif()
      foreach(_dependency IN LISTS _dependencies)
         _forge_contract_collect_library("${graph}" "${_dependency}")
      endforeach()
   endforeach()
   set_property(GLOBAL APPEND PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_NODES" "${id}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_key}_STATE" visited)
endfunction()

function(_forge_contract_library_physical_inputs target bases modules sources public_headers private_headers)
   get_target_property(_imported "${target}" IMPORTED)
   get_target_property(_module_logical "${target}" FORGE_CONTRACT_MODULE_SOURCES)
   get_target_property(_source_logical "${target}" FORGE_CONTRACT_SOURCES)
   get_target_property(_public_header_logical "${target}" FORGE_CONTRACT_PUBLIC_HEADERS)
   get_target_property(_private_header_logical "${target}" FORGE_CONTRACT_PRIVATE_HEADERS)
   if(_imported)
      _forge_contract_imported_location("${target}" _archive)
      get_filename_component(_archive_directory "${_archive}" DIRECTORY)
      get_target_property(_module_root_relative "${target}" FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE)
      get_target_property(_source_root_relative "${target}" FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE)
      get_target_property(_installed_modules "${target}" FORGE_CONTRACT_INSTALL_MODULE_PATHS)
      get_target_property(_installed_public_headers "${target}" FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS)
      get_filename_component(_module_root "${_archive_directory}/${_module_root_relative}" ABSOLUTE)
      get_filename_component(_source_root "${_archive_directory}/${_source_root_relative}" ABSOLUTE)
      set(_bases "${_module_root}")
      set(_modules)
      foreach(_path IN LISTS _installed_modules)
         list(APPEND _modules "${_module_root}/${_path}")
      endforeach()
      set(_public_headers)
      foreach(_path IN LISTS _installed_public_headers)
         list(APPEND _public_headers "${_module_root}/${_path}")
      endforeach()
   else()
      get_target_property(_source_root "${target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
      get_target_property(_base_logical "${target}" FORGE_CONTRACT_MODULE_BASE_DIRS)
      set(_bases)
      foreach(_path IN LISTS _base_logical)
         list(APPEND _bases "${_source_root}/${_path}")
      endforeach()
      set(_modules)
      foreach(_path IN LISTS _module_logical)
         list(APPEND _modules "${_source_root}/${_path}")
      endforeach()
      set(_public_headers)
      foreach(_path IN LISTS _public_header_logical)
         list(APPEND _public_headers "${_source_root}/${_path}")
      endforeach()
   endif()
   set(_sources)
   foreach(_path IN LISTS _source_logical)
      list(APPEND _sources "${_source_root}/${_path}")
   endforeach()
   set(_private_headers)
   foreach(_path IN LISTS _private_header_logical)
      list(APPEND _private_headers "${_source_root}/${_path}")
   endforeach()
   foreach(_input IN LISTS _modules _sources _public_headers _private_headers)
      if(NOT EXISTS "${_input}")
         message(FATAL_ERROR "contract descriptor input does not exist: ${_input}")
      endif()
   endforeach()
   set(${bases} "${_bases}" PARENT_SCOPE)
   set(${modules} "${_modules}" PARENT_SCOPE)
   set(${sources} "${_sources}" PARENT_SCOPE)
   set(${public_headers} "${_public_headers}" PARENT_SCOPE)
   set(${private_headers} "${_private_headers}" PARENT_SCOPE)
endfunction()

function(_forge_contract_file_json role logical physical output)
   _forge_contract_json_quote("${role}" _role)
   _forge_contract_json_quote("${logical}" _logical)
   _forge_contract_json_quote("${physical}" _physical)
   set(${output} "{\"role\":${_role},\"logical_path\":${_logical},\"physical_path\":${_physical}}" PARENT_SCOPE)
endfunction()

function(_forge_contract_edge_json kind id scope output)
   string(TOLOWER "${scope}" _scope_value)
   _forge_contract_json_quote("${kind}" _kind)
   _forge_contract_json_quote("${id}" _id)
   _forge_contract_json_quote("${_scope_value}" _scope)
   set(${output} "{\"kind\":${_kind},\"id\":${_id},\"scope\":${_scope}}" PARENT_SCOPE)
endfunction()

function(_forge_contract_write_graph target libraries output_file output_hash build_dependencies)
   string(SHA256 _graph_key "${CMAKE_CURRENT_BINARY_DIR}/${target}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph_key}_NODES" "")
   _forge_contract_register_visible_imported_libraries()

   set(_root_library_ids)
   set(_root_component_ids)
   set(_build_dependencies)
   foreach(_dependency IN LISTS libraries)
      _forge_contract_classify_dependency("${_dependency}" _kind _id _dependency_target)
      if(_kind STREQUAL "library")
         if(_id IN_LIST _root_library_ids)
            message(FATAL_ERROR "duplicate root contract library ID: ${_id}")
         endif()
         list(APPEND _root_library_ids "${_id}")
         _forge_contract_collect_library("${_graph_key}" "${_id}")
      else()
         if(_id IN_LIST _root_component_ids)
            message(FATAL_ERROR "duplicate root Forge guest component ID: ${_id}")
         endif()
         list(APPEND _root_component_ids "${_id}")
      endif()
      get_target_property(_imported "${_dependency_target}" IMPORTED)
      if(NOT _imported)
         list(APPEND _build_dependencies "${_dependency_target}")
      endif()
   endforeach()

   get_property(_node_ids GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph_key}_NODES")
   set(_nodes "[]")
   set(_node_index 0)
   foreach(_id IN LISTS _node_ids)
      _forge_contract_find_library_target("${_id}" _node_target)
      _forge_contract_library_physical_inputs(
         "${_node_target}" _bases _modules _sources _public_headers _private_headers
      )
      get_target_property(_module_logical "${_node_target}" FORGE_CONTRACT_MODULE_SOURCES)
      get_target_property(_source_logical "${_node_target}" FORGE_CONTRACT_SOURCES)
      get_target_property(_public_header_logical "${_node_target}" FORGE_CONTRACT_PUBLIC_HEADERS)
      get_target_property(_private_header_logical "${_node_target}" FORGE_CONTRACT_PRIVATE_HEADERS)

      set(_files "[]")
      set(_file_index 0)
      foreach(_role module implementation public_header private_header)
         if(_role STREQUAL "module")
            set(_logical_values ${_module_logical})
            set(_physical_values ${_modules})
         elseif(_role STREQUAL "implementation")
            set(_logical_values ${_source_logical})
            set(_physical_values ${_sources})
         elseif(_role STREQUAL "public_header")
            set(_logical_values ${_public_header_logical})
            set(_physical_values ${_public_headers})
         else()
            set(_logical_values ${_private_header_logical})
            set(_physical_values ${_private_headers})
         endif()
         list(LENGTH _logical_values _count)
         if(_count GREATER 0)
            math(EXPR _last "${_count} - 1")
            foreach(_index RANGE 0 ${_last})
               list(GET _logical_values ${_index} _logical)
               list(GET _physical_values ${_index} _physical)
               _forge_contract_file_json("${_role}" "${_logical}" "${_physical}" _file)
               string(JSON _files SET "${_files}" ${_file_index} "${_file}")
               math(EXPR _file_index "${_file_index} + 1")
            endforeach()
         endif()
      endforeach()

      set(_edges "[]")
      set(_edge_index 0)
      foreach(_scope PUBLIC PRIVATE)
         foreach(_kind LIBRARY COMPONENT)
            get_target_property(
               _ids "${_node_target}" "FORGE_CONTRACT_${_scope}_${_kind}_IDS"
            )
            if(_ids STREQUAL "_ids-NOTFOUND")
               set(_ids)
            endif()
            string(TOLOWER "${_kind}" _kind_value)
            foreach(_dependency_id IN LISTS _ids)
               _forge_contract_edge_json(
                  "${_kind_value}" "${_dependency_id}" "${_scope}" _edge
               )
               string(JSON _edges SET "${_edges}" ${_edge_index} "${_edge}")
               math(EXPR _edge_index "${_edge_index} + 1")
            endforeach()
         endforeach()
      endforeach()

      _forge_contract_json_quote("${_id}" _quoted_id)
      _forge_contract_json_array("${_bases}" _bases_json)
      set(
         _node
         "{\"id\":${_quoted_id},\"module_bases\":${_bases_json},\"files\":${_files},\"dependencies\":${_edges}}"
      )
      string(JSON _nodes SET "${_nodes}" ${_node_index} "${_node}")
      math(EXPR _node_index "${_node_index} + 1")
   endforeach()

   _forge_contract_json_array("${_root_library_ids}" _root_libraries)
   _forge_contract_json_array("${_root_component_ids}" _root_components)
   set(
      _json
      "{\"schema\":2,\"root\":{\"libraries\":${_root_libraries},\"components\":${_root_components}},\"libraries\":${_nodes}}"
   )
   set(_path "${CMAKE_CURRENT_BINARY_DIR}/${target}.contract-graph.json")
   file(WRITE "${_path}" "${_json}\n")
   file(SHA256 "${_path}" _hash)
   set(${output_file} "${_path}" PARENT_SCOPE)
   set(${output_hash} "${_hash}" PARENT_SCOPE)
   set(${build_dependencies} "${_build_dependencies}" PARENT_SCOPE)
endfunction()

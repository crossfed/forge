function(_forge_contract_id_key id output)
   string(SHA256 _key "${id}")
   set(${output} "${_key}" PARENT_SCOPE)
endfunction()

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

function(_forge_contract_normalize_root input description output)
   get_filename_component(
      _absolute "${input}" REALPATH
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
   )
   if(NOT IS_DIRECTORY "${_absolute}")
      message(FATAL_ERROR "${description} is not a directory: ${_absolute}")
   endif()
   set(${output} "${_absolute}" PARENT_SCOPE)
endfunction()

function(_forge_contract_normalize_file root input description output)
   get_filename_component(_absolute "${input}" REALPATH BASE_DIR "${root}")
   if(NOT EXISTS "${_absolute}" OR IS_DIRECTORY "${_absolute}")
      message(FATAL_ERROR "${description} does not exist or is not a file: ${_absolute}")
   endif()
   set(${output} "${_absolute}" PARENT_SCOPE)
endfunction()

function(_forge_contract_configure_guest_target target)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   target_include_directories(
      "${target}" PRIVATE "${ForgeContract_DATA_DIR}/include"
   )
   target_compile_definitions("${target}" PRIVATE FORGE_CONTRACT_GUEST=1)
   target_compile_options(
      "${target}"
      PRIVATE
         "-fplugin=${ForgeContract_ATTR_PLUGIN}"
         -fno-exceptions
         -fno-rtti
         -fno-threadsafe-statics
         -ffreestanding
         -fvisibility=hidden
         -fno-ident
         -mcpu=mvp
         -O3
   )
   set_property(TARGET "${target}" PROPERTY CXX_COMPILER_LAUNCHER "")
endfunction()

function(_forge_contract_register_owner id target)
   _forge_contract_id_key("${id}" _key)
   get_property(_existing GLOBAL PROPERTY "FORGE_CONTRACT_OWNER_${_key}")
   if(_existing AND NOT _existing STREQUAL target)
      message(FATAL_ERROR "duplicate Forge Contract owner ID: ${id}")
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_OWNER_${_key}" "${target}")
endfunction()

function(_forge_contract_owner_target id output)
   _forge_contract_id_key("${id}" _key)
   get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_OWNER_${_key}")
   if(NOT _target)
      message(FATAL_ERROR "Forge Contract owner is not visible: ${id}")
   endif()
   set(${output} "${_target}" PARENT_SCOPE)
endfunction()

if(NOT COMMAND forge_contract_register_guest_component)
   function(forge_contract_register_guest_component)
      cmake_parse_arguments(
         ARG
         "FOUNDATION"
         "ID;TARGET;ARCHIVE"
         "MODULES;MODULE_NAMES;PUBLIC_LIBRARIES"
         ${ARGN}
      )
      if(
         ARG_UNPARSED_ARGUMENTS
         OR NOT ARG_ID
         OR NOT ARG_TARGET
         OR NOT ARG_MODULES
         OR NOT ARG_MODULE_NAMES
      )
         message(FATAL_ERROR "invalid Forge Contract guest component declaration")
      endif()
      list(LENGTH ARG_MODULES _module_count)
      list(LENGTH ARG_MODULE_NAMES _module_name_count)
      if(NOT _module_count EQUAL _module_name_count)
         message(
            FATAL_ERROR
            "Forge Contract guest component ${ARG_ID} has mismatched module metadata"
         )
      endif()

      _forge_contract_id_key("${ARG_ID}" _key)
      get_property(
         _existing GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_ID"
      )
      if(_existing)
         message(FATAL_ERROR "duplicate Forge Contract component ID: ${ARG_ID}")
      endif()
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_ID" "${ARG_ID}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_TARGET_NAME"
         "${ARG_TARGET}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_MODULE_NAMES"
         "${ARG_MODULE_NAMES}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_DEPENDENCY_IDS"
         "${ARG_PUBLIC_LIBRARIES}"
      )
      if(ARG_FOUNDATION)
         set_property(
            GLOBAL APPEND PROPERTY FORGE_CONTRACT_FOUNDATION_COMPONENT_IDS
            "${ARG_ID}"
         )
      endif()

      if(NOT FORGE_CONTRACT_GUEST)
         return()
      endif()
      if(TARGET "Forge::${ARG_TARGET}")
         message(
            FATAL_ERROR
            "Forge Contract guest target already exists: Forge::${ARG_TARGET}"
         )
      endif()

      string(SUBSTRING "${_key}" 0 12 _short_key)
      set(_target "_forge_contract_guest_${ARG_TARGET}_${_short_key}")
      set(_module_paths)
      foreach(_module IN LISTS ARG_MODULES)
         set(_path "${ForgeContract_DATA_DIR}/modules/${_module}")
         if(NOT EXISTS "${_path}")
            message(
               FATAL_ERROR
               "Forge Contract guest component module is missing: ${_path}"
            )
         endif()
         list(APPEND _module_paths "${_path}")
      endforeach()

      add_library("${_target}" STATIC)
      add_library("Forge::${ARG_TARGET}" ALIAS "${_target}")
      target_sources(
         "${_target}"
         PUBLIC
            FILE_SET forge_contract_guest_modules TYPE CXX_MODULES
            BASE_DIRS "${ForgeContract_DATA_DIR}/modules"
            FILES ${_module_paths}
      )
      set_source_files_properties(
         ${_module_paths}
         PROPERTIES OBJECT_DEPENDS "${ForgeContract_ATTR_PLUGIN}"
      )
      if(ARG_ARCHIVE)
         set(_archive "${ForgeContract_SYSROOT}/lib/${ARG_ARCHIVE}")
         if(NOT EXISTS "${_archive}")
            message(
               FATAL_ERROR
               "Forge Contract guest component archive is missing: ${_archive}"
            )
         endif()
         add_library("${_target}_archive" STATIC IMPORTED GLOBAL)
         set_target_properties(
            "${_target}_archive" PROPERTIES IMPORTED_LOCATION "${_archive}"
         )
         target_link_libraries("${_target}" PUBLIC "${_target}_archive")
      endif()
      target_compile_features("${_target}" PUBLIC cxx_std_23)
      set_target_properties(
         "${_target}"
         PROPERTIES
            CXX_MODULE_STD OFF
            CXX_SCAN_FOR_MODULES ON
            FORGE_CONTRACT_OWNER_ID "${ARG_ID}"
            FORGE_CONTRACT_COMPONENT TRUE
            FORGE_CONTRACT_MODULE_NAMES "${ARG_MODULE_NAMES}"
            FORGE_CONTRACT_PUBLIC_OWNER_IDS "${ARG_PUBLIC_LIBRARIES}"
            FORGE_CONTRACT_PRIVATE_OWNER_IDS ""
            FORGE_CONTRACT_MODULE_BASES "${ForgeContract_DATA_DIR}/modules"
      )
      _forge_contract_configure_guest_target("${_target}")
      _forge_contract_register_owner("${ARG_ID}" "${_target}")
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_TARGET" "${_target}"
      )
      set_property(
         GLOBAL APPEND PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS "${ARG_ID}"
      )
   endfunction()
endif()

function(_forge_contract_finalize_guest_components)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   get_property(_ids GLOBAL PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS)
   foreach(_id IN LISTS _ids)
      _forge_contract_owner_target("${_id}" _target)
      _forge_contract_id_key("${_id}" _key)
      get_property(
         _dependencies GLOBAL
         PROPERTY "FORGE_CONTRACT_COMPONENT_${_key}_DEPENDENCY_IDS"
      )
      foreach(_dependency IN LISTS _dependencies)
         _forge_contract_owner_target("${_dependency}" _dependency_target)
         target_link_libraries("${_target}" PUBLIC "${_dependency_target}")
      endforeach()
   endforeach()

   _forge_contract_owner_target("forge.contract.runtime" _runtime_target)
   if(NOT TARGET _forge_contract_guest_eosio)
      add_library(_forge_contract_guest_eosio INTERFACE)
      target_include_directories(
         _forge_contract_guest_eosio
         INTERFACE "${ForgeContract_DATA_DIR}/include"
      )
      target_link_libraries(
         _forge_contract_guest_eosio INTERFACE "${_runtime_target}"
      )
   endif()
   set_property(
      GLOBAL PROPERTY FORGE_CONTRACT_GUEST_EOSIO_TARGET
      _forge_contract_guest_eosio
   )
endfunction()

function(_forge_contract_dependency input scope target_output owner_output)
   _forge_contract_resolve_target("${input}" _target)
   get_target_property(_owner "${_target}" FORGE_CONTRACT_OWNER_ID)
   if(NOT _owner)
      get_target_property(
         _owner "${_target}" FORGE_CONTRACT_GUEST_COMPONENT_ID
      )
   endif()
   if(NOT _owner)
      message(
         FATAL_ERROR
         "${scope} contract dependency is not guest-compatible: ${input}"
      )
   endif()
   set(${target_output} "${_target}" PARENT_SCOPE)
   set(${owner_output} "${_owner}" PARENT_SCOPE)
endfunction()

function(forge_add_contract_library target)
   cmake_parse_arguments(
      ARG
      ""
      "ID"
      "MODULE_BASE_DIRS;MODULE_SOURCES;SOURCES;PUBLIC_LIBRARIES;PRIVATE_LIBRARIES"
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(
         FATAL_ERROR
         "forge_add_contract_library(${target}) received unknown arguments: "
         "${ARG_UNPARSED_ARGUMENTS}"
      )
   endif()
   if(TARGET "${target}")
      message(
         FATAL_ERROR
         "forge_add_contract_library target already exists: ${target}"
      )
   endif()
   if(NOT ARG_ID OR NOT ARG_ID MATCHES "^[A-Za-z0-9][A-Za-z0-9_.-]*$")
      message(
         FATAL_ERROR
         "forge_add_contract_library(${target}) requires a canonical ID"
      )
   endif()
   if(NOT ARG_MODULE_BASE_DIRS OR NOT ARG_MODULE_SOURCES)
      message(
         FATAL_ERROR
         "forge_add_contract_library(${target}) requires "
         "MODULE_BASE_DIRS and MODULE_SOURCES"
      )
   endif()
   set(_source_root "${CMAKE_CURRENT_SOURCE_DIR}")

   set(_module_bases)
   foreach(_base IN LISTS ARG_MODULE_BASE_DIRS)
      get_filename_component(
         _absolute "${_base}" REALPATH BASE_DIR "${_source_root}"
      )
      if(NOT IS_DIRECTORY "${_absolute}")
         message(
            FATAL_ERROR
            "contract module base is not a directory: ${_absolute}"
         )
      endif()
      list(APPEND _module_bases "${_absolute}")
   endforeach()
   list(REMOVE_DUPLICATES _module_bases)

   foreach(_role MODULE_SOURCES SOURCES)
      set(_files)
      foreach(_file IN LISTS ARG_${_role})
         _forge_contract_normalize_file(
            "${_source_root}" "${_file}" "contract ${_role} file" _absolute
         )
         if(_absolute IN_LIST _all_files)
            message(
               FATAL_ERROR
               "contract file is declared more than once: ${_absolute}"
            )
         endif()
         list(APPEND _all_files "${_absolute}")
         list(APPEND _files "${_absolute}")
      endforeach()
      set(_${_role} "${_files}")
   endforeach()

   set(_public_targets)
   set(_public_ids)
   foreach(_dependency IN LISTS ARG_PUBLIC_LIBRARIES)
      _forge_contract_dependency(
         "${_dependency}" "PUBLIC" _dependency_target _dependency_id
      )
      if(_dependency_id IN_LIST _public_ids OR _dependency_id IN_LIST _private_ids)
         message(FATAL_ERROR "duplicate contract dependency owner: ${_dependency_id}")
      endif()
      list(APPEND _public_targets "${_dependency_target}")
      list(APPEND _public_ids "${_dependency_id}")
   endforeach()
   set(_private_targets)
   set(_private_ids)
   foreach(_dependency IN LISTS ARG_PRIVATE_LIBRARIES)
      _forge_contract_dependency(
         "${_dependency}" "PRIVATE" _dependency_target _dependency_id
      )
      if(_dependency_id IN_LIST _public_ids OR _dependency_id IN_LIST _private_ids)
         message(FATAL_ERROR "duplicate contract dependency owner: ${_dependency_id}")
      endif()
      list(APPEND _private_targets "${_dependency_target}")
      list(APPEND _private_ids "${_dependency_id}")
   endforeach()

   string(MAKE_C_IDENTIFIER "${target}" _target_identifier)
   string(
      SHA256 _target_key
      "${CMAKE_CURRENT_BINARY_DIR}\n${target}\n${ARG_ID}"
   )
   string(SUBSTRING "${_target_key}" 0 12 _target_key)
   set(_concrete_target "_forge_contract_library_${_target_identifier}_${_target_key}")
   add_library("${_concrete_target}" STATIC)
   add_library("${target}" ALIAS "${_concrete_target}")
   target_sources(
      "${_concrete_target}"
      PUBLIC
         FILE_SET forge_contract_modules TYPE CXX_MODULES
         BASE_DIRS ${_module_bases}
         FILES ${_MODULE_SOURCES}
   )
   if(_SOURCES)
      target_sources("${_concrete_target}" PRIVATE ${_SOURCES})
   endif()
   target_include_directories(
      "${_concrete_target}"
      PUBLIC ${_module_bases}
      PRIVATE "${_source_root}"
   )
   if(_public_targets)
      target_link_libraries("${_concrete_target}" PUBLIC ${_public_targets})
   endif()
   if(_private_targets)
      target_link_libraries("${_concrete_target}" PRIVATE ${_private_targets})
   endif()
   target_compile_features("${_concrete_target}" PUBLIC cxx_std_23)
   set_target_properties(
      "${_concrete_target}"
      PROPERTIES
         CXX_MODULE_STD OFF
         CXX_SCAN_FOR_MODULES ON
         FORGE_CONTRACT_OWNER_ID "${ARG_ID}"
         FORGE_CONTRACT_LIBRARY TRUE
         FORGE_CONTRACT_PUBLIC_OWNER_IDS "${_public_ids}"
         FORGE_CONTRACT_PRIVATE_OWNER_IDS "${_private_ids}"
         FORGE_CONTRACT_MODULE_BASES "${_module_bases}"
         FORGE_CONTRACT_MODULE_NAMES ""
   )
   _forge_contract_configure_guest_target("${_concrete_target}")
   _forge_contract_register_owner("${ARG_ID}" "${_concrete_target}")
endfunction()

function(_forge_contract_collect_owner id state_key)
   _forge_contract_id_key("${id}" _key)
   get_property(_state GLOBAL PROPERTY "${state_key}_${_key}_STATE")
   if(_state STREQUAL "done")
      return()
   endif()
   if(_state STREQUAL "visiting")
      message(FATAL_ERROR "cycle in Forge Contract dependencies at ${id}")
   endif()
   set_property(GLOBAL PROPERTY "${state_key}_${_key}_STATE" visiting)
   _forge_contract_owner_target("${id}" _target)
   foreach(_property FORGE_CONTRACT_PUBLIC_OWNER_IDS FORGE_CONTRACT_PRIVATE_OWNER_IDS)
      get_target_property(_dependencies "${_target}" "${_property}")
      if(_dependencies STREQUAL "_dependencies-NOTFOUND")
         set(_dependencies)
      endif()
      foreach(_dependency IN LISTS _dependencies)
         _forge_contract_collect_owner("${_dependency}" "${state_key}")
      endforeach()
   endforeach()
   set_property(GLOBAL APPEND PROPERTY "${state_key}_IDS" "${id}")
   set_property(GLOBAL PROPERTY "${state_key}_${_key}_STATE" done)
endfunction()

function(_forge_contract_collect_dependencies)
   cmake_parse_arguments(
      ARG
      ""
      "KEY;OWNER_IDS;TARGETS;MODULE_BASES"
      "LIBRARIES"
      ${ARGN}
   )
   foreach(_required KEY OWNER_IDS TARGETS MODULE_BASES)
      if(NOT ARG_${_required})
         message(FATAL_ERROR "_forge_contract_collect_dependencies requires ${_required}")
      endif()
   endforeach()
   set_property(GLOBAL PROPERTY "${ARG_KEY}_IDS" "")

   get_property(
      _foundation GLOBAL PROPERTY FORGE_CONTRACT_FOUNDATION_COMPONENT_IDS
   )
   foreach(_owner IN LISTS _foundation)
      _forge_contract_collect_owner("${_owner}" "${ARG_KEY}")
   endforeach()
   foreach(_dependency IN LISTS ARG_LIBRARIES)
      _forge_contract_dependency(
         "${_dependency}" "root" _dependency_target _owner
      )
      _forge_contract_collect_owner("${_owner}" "${ARG_KEY}")
   endforeach()

   get_property(_ids GLOBAL PROPERTY "${ARG_KEY}_IDS")
   list(REMOVE_DUPLICATES _ids)
   set(_targets)
   set(_module_bases)
   foreach(_id IN LISTS _ids)
      _forge_contract_owner_target("${_id}" _target)
      list(APPEND _targets "${_target}")
      get_target_property(_bases "${_target}" FORGE_CONTRACT_MODULE_BASES)
      if(_bases AND NOT _bases STREQUAL "_bases-NOTFOUND")
         list(APPEND _module_bases ${_bases})
      endif()
   endforeach()
   list(REMOVE_DUPLICATES _targets)
   list(REMOVE_DUPLICATES _module_bases)
   set(${ARG_OWNER_IDS} "${_ids}" PARENT_SCOPE)
   set(${ARG_TARGETS} "${_targets}" PARENT_SCOPE)
   set(${ARG_MODULE_BASES} "${_module_bases}" PARENT_SCOPE)
endfunction()

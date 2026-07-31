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

function(_forge_contract_product_source_root output)
   if(
      DEFINED FORGE_CONTRACT_SOURCE_ROOT
      AND NOT "${FORGE_CONTRACT_SOURCE_ROOT}" STREQUAL ""
   )
      set(_candidate "${FORGE_CONTRACT_SOURCE_ROOT}")
   else()
      set(_candidate "${CMAKE_SOURCE_DIR}")
   endif()
   get_filename_component(
      _candidate "${_candidate}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}"
   )
   if(NOT IS_DIRECTORY "${_candidate}")
      message(
         FATAL_ERROR
         "Forge Contract product source root is not a directory: ${_candidate}"
      )
   endif()
   get_property(
      _root GLOBAL PROPERTY FORGE_CONTRACT_CANONICAL_SOURCE_ROOT
   )
   if(_root AND NOT "${_candidate}" STREQUAL "${_root}")
      message(
         FATAL_ERROR
         "FORGE_CONTRACT_SOURCE_ROOT changed after the guest SDK fixed its "
         "canonical source root: ${_root} -> ${_candidate}"
      )
   endif()
   if(NOT _root)
      set(_root "${_candidate}")
      set_property(
         GLOBAL PROPERTY FORGE_CONTRACT_CANONICAL_SOURCE_ROOT "${_root}"
      )
   endif()
   set(${output} "${_root}" PARENT_SCOPE)
endfunction()

function(_forge_contract_require_product_source path description)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   _forge_contract_product_source_root(_product_root)
   get_filename_component(_absolute "${path}" REALPATH)
   file(RELATIVE_PATH _relative "${_product_root}" "${_absolute}")
   if(IS_ABSOLUTE "${_relative}" OR _relative MATCHES "^\\.\\.(/|$)")
      message(
         FATAL_ERROR
         "${description} is outside FORGE_CONTRACT_SOURCE_ROOT: ${_absolute}\n"
         "product source root: ${_product_root}"
      )
   endif()
endfunction()

function(_forge_contract_semantic_properties output)
   set(
      ${output}
      SOURCES
      INTERFACE_SOURCES
      COMPILE_OPTIONS
      INTERFACE_COMPILE_OPTIONS
      COMPILE_DEFINITIONS
      INTERFACE_COMPILE_DEFINITIONS
      INCLUDE_DIRECTORIES
      INTERFACE_INCLUDE_DIRECTORIES
      COMPILE_FEATURES
      INTERFACE_COMPILE_FEATURES
      LINK_OPTIONS
      INTERFACE_LINK_OPTIONS
      # These values are compared verbatim only. Forge never interprets them as
      # a second dependency graph.
      LINK_LIBRARIES
      INTERFACE_LINK_LIBRARIES
      CXX_STANDARD
      CXX_STANDARD_REQUIRED
      CXX_EXTENSIONS
      CXX_MODULE_STD
      CXX_SCAN_FOR_MODULES
      PARENT_SCOPE
   )
endfunction()

function(_forge_contract_validate_guest_targets)
   _forge_contract_validate_guest_environment()
   get_property(_targets GLOBAL PROPERTY FORGE_CONTRACT_FROZEN_TARGETS)
   foreach(target IN LISTS _targets)
      if(NOT TARGET "${target}")
         message(FATAL_ERROR "frozen Forge Contract guest target disappeared: ${target}")
      endif()
      get_target_property(
         _declaration "${target}" FORGE_CONTRACT_FROZEN_DECLARATION
      )
      _forge_contract_semantic_properties(_properties)
      foreach(_property IN LISTS _properties)
         get_property(_is_set TARGET "${target}" PROPERTY "${_property}" SET)
         if(_is_set)
            get_target_property(_current "${target}" "${_property}")
         else()
            set(_current "<FORGE_UNSET>")
         endif()
         get_target_property(
            _expected "${target}" "FORGE_CONTRACT_FROZEN_${_property}"
         )
         if(NOT "${_current}" STREQUAL "${_expected}")
            message(
               FATAL_ERROR
               "Forge Contract guest target '${target}' was modified after "
               "${_declaration}; post-declaration target mutation is unsupported "
               "because CMake compilation and Abigen must use one semantic profile "
               "(changed property: ${_property})"
            )
         endif()
      endforeach()
   endforeach()
endfunction()

function(_forge_contract_freeze_guest_target target declaration)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   _forge_contract_semantic_properties(_properties)
   foreach(_property IN LISTS _properties)
      get_property(_is_set TARGET "${target}" PROPERTY "${_property}" SET)
      if(_is_set)
         get_target_property(_current "${target}" "${_property}")
      else()
         set(_current "<FORGE_UNSET>")
      endif()
      set_property(
         TARGET "${target}" PROPERTY
         "FORGE_CONTRACT_FROZEN_${_property}" "${_current}"
      )
   endforeach()
   set_property(
      TARGET "${target}" PROPERTY
      FORGE_CONTRACT_FROZEN_DECLARATION "${declaration}"
   )
   get_target_property(_source_directory "${target}" SOURCE_DIR)
   string(SHA256 _directory_key "${_source_directory}")
   get_property(
      _directory_scheduled GLOBAL PROPERTY
      "FORGE_CONTRACT_ENVIRONMENT_VALIDATION_${_directory_key}"
   )
   if(NOT _directory_scheduled)
      set_property(
         GLOBAL PROPERTY
         "FORGE_CONTRACT_ENVIRONMENT_VALIDATION_${_directory_key}" TRUE
      )
      cmake_language(
         DEFER DIRECTORY "${_source_directory}"
         CALL _forge_contract_validate_guest_environment
      )
   endif()
   set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_FROZEN_TARGETS "${target}")
   get_property(
      _scheduled GLOBAL PROPERTY FORGE_CONTRACT_FROZEN_VALIDATION_SCHEDULED
   )
   if(NOT _scheduled)
      set_property(
         GLOBAL PROPERTY FORGE_CONTRACT_FROZEN_VALIDATION_SCHEDULED TRUE
      )
      cmake_language(
         DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
         CALL _forge_contract_validate_guest_targets
      )
   endif()
endfunction()

function(_forge_contract_validate_guest_environment)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   if(NOT "${CMAKE_CXX_FLAGS}" STREQUAL "")
      message(
         FATAL_ERROR
         "CMAKE_CXX_FLAGS must remain empty in a Forge Contract guest project; "
         "the SDK owns the compile profile shared by CMake and Abigen"
      )
   endif()
   if(
      NOT CMAKE_CXX_STANDARD EQUAL 23
      OR NOT CMAKE_CXX_STANDARD_REQUIRED
      OR CMAKE_CXX_EXTENSIONS
      OR NOT CMAKE_CXX_SCAN_FOR_MODULES
   )
      message(
         FATAL_ERROR
         "Forge Contract guest projects require strict C++23 with required "
         "standard, extensions disabled, and CMake module scanning enabled"
      )
   endif()
   foreach(_entry
      "CMAKE_CXX_FLAGS_DEBUG|-g"
      "CMAKE_CXX_FLAGS_RELEASE|-O3 -DNDEBUG"
      "CMAKE_CXX_FLAGS_MINSIZEREL|-Os -DNDEBUG"
      "CMAKE_CXX_FLAGS_RELWITHDEBINFO|-O2 -g -DNDEBUG"
   )
      string(REPLACE "|" ";" _parts "${_entry}")
      list(GET _parts 0 _variable)
      list(GET _parts 1 _expected)
      if(NOT "${${_variable}}" STREQUAL "${_expected}")
         message(
            FATAL_ERROR
            "${_variable} must remain '${_expected}' in a Forge Contract guest "
            "project; the SDK owns the compile profile shared by CMake and Abigen"
         )
      endif()
   endforeach()
   foreach(_property COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES)
      get_directory_property(_value "${_property}")
      if(_value)
         message(
            FATAL_ERROR
            "directory ${_property} are unsupported in a Forge Contract guest "
            "project; declare the complete target through Forge Contract helpers"
         )
      endif()
   endforeach()
endfunction()

function(_forge_contract_configure_guest_target target)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   _forge_contract_validate_guest_environment()
   _forge_contract_product_source_root(_product_source_root)
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
         "-ffile-prefix-map=${_product_source_root}=./source"
         "-fdebug-prefix-map=${_product_source_root}=./source"
         "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build"
         "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=./build"
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

function(_forge_contract_object_list target identity output)
   string(
      SHA256 _property_key
      "${CMAKE_CURRENT_BINARY_DIR}\n${target}\n${identity}"
   )
   get_property(
      _existing GLOBAL PROPERTY "FORGE_CONTRACT_OBJECT_LIST_${_property_key}"
   )
   if(_existing)
      set(${output} "${_existing}" PARENT_SCOPE)
      return()
   endif()

   set(
      _path
      "${CMAKE_CURRENT_BINARY_DIR}/contract-compilations/${identity}-$<CONFIG>.objects"
   )
   file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contract-compilations")
   file(
      GENERATE
      OUTPUT "${_path}"
      CONTENT "$<JOIN:$<TARGET_OBJECTS:${target}>,\n>\n"
   )
   set_property(
      GLOBAL PROPERTY "FORGE_CONTRACT_OBJECT_LIST_${_property_key}" "${_path}"
   )
   set(${output} "${_path}" PARENT_SCOPE)
endfunction()

function(_forge_contract_module_path target output)
   get_target_property(_binary_dir "${target}" BINARY_DIR)
   if(NOT _binary_dir)
      message(FATAL_ERROR "cannot determine module build directory for ${target}")
   endif()
   set(_configuration_directory)
   if(CMAKE_CONFIGURATION_TYPES)
      set(_configuration_directory "$<CONFIG>/")
   endif()
   set(
      ${output}
      "${_binary_dir}/CMakeFiles/${target}.dir/${_configuration_directory}"
      PARENT_SCOPE
   )
endfunction()

function(_forge_contract_enable_metadata_closure target)
   set_property(
      TARGET "${target}" APPEND PROPERTY TRANSITIVE_LINK_PROPERTIES
      FORGE_CONTRACT_COMPILATIONS
      FORGE_CONTRACT_MODULE_BASES
      FORGE_CONTRACT_MODULE_PATHS
      FORGE_CONTRACT_OWNER_TARGETS
   )
endfunction()

function(_forge_contract_publish_metadata target)
   cmake_parse_arguments(
      ARG
      ""
      "COMPILATION;MODULE_PATH"
      "MODULE_BASES"
      ${ARGN}
   )
   _forge_contract_enable_metadata_closure("${target}")
   if(ARG_COMPILATION)
      set_property(
         TARGET "${target}" APPEND PROPERTY
         INTERFACE_FORGE_CONTRACT_COMPILATIONS "${ARG_COMPILATION}"
      )
   endif()
   if(ARG_MODULE_BASES)
      set_property(
         TARGET "${target}" APPEND PROPERTY
         INTERFACE_FORGE_CONTRACT_MODULE_BASES "${ARG_MODULE_BASES}"
      )
   endif()
   if(ARG_MODULE_PATH)
      set_property(
         TARGET "${target}" APPEND PROPERTY
         INTERFACE_FORGE_CONTRACT_MODULE_PATHS "${ARG_MODULE_PATH}"
      )
   endif()
   set_property(
      TARGET "${target}" APPEND PROPERTY
      INTERFACE_FORGE_CONTRACT_OWNER_TARGETS "${target}"
   )
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
            FORGE_CONTRACT_MODULE_BASES "${ForgeContract_DATA_DIR}/modules"
      )
      _forge_contract_configure_guest_target("${_target}")
      _forge_contract_module_path("${_target}" _module_path)
      _forge_contract_publish_metadata(
         "${_target}"
         MODULE_BASES "${ForgeContract_DATA_DIR}/modules"
         MODULE_PATH "${_module_path}"
      )
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
   _forge_contract_require_product_source(
      "${_source_root}" "contract library source root"
   )

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

   add_library("${target}" STATIC)
   target_sources(
      "${target}"
      PUBLIC
         FILE_SET forge_contract_modules TYPE CXX_MODULES
         BASE_DIRS ${_module_bases}
         FILES ${_MODULE_SOURCES}
   )
   if(_SOURCES)
      target_sources("${target}" PRIVATE ${_SOURCES})
   endif()
   target_include_directories(
      "${target}" PRIVATE "$<BUILD_INTERFACE:${_source_root}>"
   )
   if(_public_targets)
      target_link_libraries("${target}" PUBLIC ${_public_targets})
   endif()
   if(_private_targets)
      target_link_libraries("${target}" PRIVATE ${_private_targets})
   endif()
   target_compile_features("${target}" PUBLIC cxx_std_23)
   set_target_properties(
      "${target}"
      PROPERTIES
         CXX_MODULE_STD OFF
         CXX_SCAN_FOR_MODULES ON
         FORGE_CONTRACT_OWNER_ID "${ARG_ID}"
         FORGE_CONTRACT_LIBRARY TRUE
         FORGE_CONTRACT_MODULE_BASES "${_module_bases}"
         FORGE_CONTRACT_MODULE_NAMES ""
   )
   _forge_contract_configure_guest_target("${target}")
   if(FORGE_CONTRACT_GUEST)
      _forge_contract_id_key("${ARG_ID}" _owner_key)
      _forge_contract_object_list(
         "${target}" "library-${_owner_key}" _object_list
      )
      _forge_contract_module_path("${target}" _module_path)
      _forge_contract_publish_metadata(
         "${target}"
         COMPILATION "${ARG_ID}|${_object_list}"
         MODULE_BASES ${_module_bases}
         MODULE_PATH "${_module_path}"
      )
   endif()
   _forge_contract_register_owner("${ARG_ID}" "${target}")
   _forge_contract_freeze_guest_target(
      "${target}" "forge_add_contract_library(${target})"
   )
endfunction()

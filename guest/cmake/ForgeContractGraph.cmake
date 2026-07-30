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
   FORGE_CONTRACT_PACKAGE_FINGERPRINT
   FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE
   FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE
   FORGE_CONTRACT_INSTALL_MODULE_PATHS
   FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS
)

function(_forge_contract_sealed_target_properties target output)
   set(
      _properties
      SOURCES
      INTERFACE_SOURCES
      LINK_LIBRARIES
      INTERFACE_LINK_LIBRARIES
      INCLUDE_DIRECTORIES
      INTERFACE_INCLUDE_DIRECTORIES
      COMPILE_DEFINITIONS
      INTERFACE_COMPILE_DEFINITIONS
      COMPILE_FEATURES
      INTERFACE_COMPILE_FEATURES
      COMPILE_FLAGS
      COMPILE_OPTIONS
      INTERFACE_COMPILE_OPTIONS
      COMPILE_WARNING_AS_ERROR
      LINK_DIRECTORIES
      INTERFACE_LINK_DIRECTORIES
      INTERFACE_LINK_DEPENDS
      LINK_FLAGS
      LINK_OPTIONS
      INTERFACE_LINK_OPTIONS
      INTERFACE_LINK_LIBRARIES_DIRECT
      INTERFACE_LINK_LIBRARIES_DIRECT_EXCLUDE
      INTERFACE_POSITION_INDEPENDENT_CODE
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
      INTERFACE_HEADER_SETS_TO_VERIFY
      INTERPROCEDURAL_OPTIMIZATION
      LINK_DEPENDS
      LINK_DEPENDS_NO_SHARED
      LINK_INTERFACE_LIBRARIES
      LINK_INTERFACE_MULTIPLICITY
      LINK_LIBRARIES_ONLY_TARGETS
      LINK_LIBRARIES_STRATEGY
      LINK_LIBRARY_OVERRIDE
      LINK_SEARCH_END_STATIC
      LINK_SEARCH_START_STATIC
      LINK_WHAT_YOU_USE
      OPTIMIZE_DEPENDENCIES
      TRANSITIVE_COMPILE_PROPERTIES
      TRANSITIVE_LINK_PROPERTIES
      COMPATIBLE_INTERFACE_BOOL
      COMPATIBLE_INTERFACE_NUMBER_MAX
      COMPATIBLE_INTERFACE_NUMBER_MIN
      COMPATIBLE_INTERFACE_STRING
      PRECOMPILE_HEADERS
      INTERFACE_PRECOMPILE_HEADERS
      PRECOMPILE_HEADERS_REUSE_FROM
      MANUALLY_ADDED_DEPENDENCIES
      CXX_STANDARD
      CXX_STANDARD_REQUIRED
      CXX_EXTENSIONS
      CXX_MODULE_STD
      CXX_SCAN_FOR_MODULES
      POSITION_INDEPENDENT_CODE
      STATIC_LIBRARY_OPTIONS
      UNITY_BUILD
      UNITY_BUILD_BATCH_SIZE
      UNITY_BUILD_MODE
      VERIFY_INTERFACE_HEADER_SETS
      CXX_MODULE_SETS
      INTERFACE_CXX_MODULE_SETS
      CXX_MODULE_SET_forge_contract_modules
      CXX_MODULE_DIRS_forge_contract_modules
      HEADER_SETS
      INTERFACE_HEADER_SETS
      HEADER_SET_forge_contract_public_headers
      HEADER_DIRS_forge_contract_public_headers
      OUTPUT_NAME
      EXPORT_NAME
      EXPORT_PROPERTIES
      EXPORT_NO_SYSTEM
      NO_SYSTEM_FROM_IMPORTED
      SYSTEM
      FORGE_CONTRACT_BUILD_SOURCE_ROOT
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
      FORGE_CONTRACT_PUBLIC_LIBRARY_TARGETS
      FORGE_CONTRACT_PRIVATE_LIBRARY_TARGETS
      FORGE_CONTRACT_PACKAGE_FINGERPRINT
      FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE
      FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE
      FORGE_CONTRACT_INSTALL_MODULE_PATHS
      FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS
      FORGE_CONTRACT_INSTALL_MODULE_DESTINATION
      FORGE_CONTRACT_INSTALL_SOURCE_DESTINATION
   )
   set(
      _build_configurations
      ${CMAKE_BUILD_TYPE}
      ${CMAKE_CONFIGURATION_TYPES}
      DEBUG
      RELEASE
      RELWITHDEBINFO
      MINSIZEREL
   )
   list(REMOVE_DUPLICATES _build_configurations)
   foreach(_configuration IN LISTS _build_configurations)
      string(TOUPPER "${_configuration}" _configuration)
      list(
         APPEND _properties
         "COMPILE_DEFINITIONS_${_configuration}"
         "INTERPROCEDURAL_OPTIMIZATION_${_configuration}"
         "LINK_FLAGS_${_configuration}"
         "LINK_INTERFACE_LIBRARIES_${_configuration}"
         "LINK_INTERFACE_MULTIPLICITY_${_configuration}"
      )
   endforeach()
   get_target_property(_imported "${target}" IMPORTED)
   if(_imported)
      list(
         APPEND _properties
         IMPORTED_CONFIGURATIONS
         IMPORTED_CXX_MODULES_COMPILE_DEFINITIONS
         IMPORTED_CXX_MODULES_COMPILE_FEATURES
         IMPORTED_CXX_MODULES_COMPILE_OPTIONS
         IMPORTED_CXX_MODULES_INCLUDE_DIRECTORIES
         IMPORTED_CXX_MODULES_LINK_LIBRARIES
         IMPORTED_IMPLIB
         IMPORTED_LIBNAME
         IMPORTED_LINK_DEPENDENT_LIBRARIES
         IMPORTED_LINK_INTERFACE_LANGUAGES
         IMPORTED_LINK_INTERFACE_LIBRARIES
         IMPORTED_LINK_INTERFACE_MULTIPLICITY
         IMPORTED_LOCATION
         IMPORTED_NO_SONAME
         IMPORTED_NO_SYSTEM
         IMPORTED_OBJECTS
         IMPORTED_SONAME
      )
      get_target_property(_configurations "${target}" IMPORTED_CONFIGURATIONS)
      if(_configurations STREQUAL "_configurations-NOTFOUND")
         set(_configurations)
      endif()
      list(APPEND _configurations ${_build_configurations})
      list(REMOVE_DUPLICATES _configurations)
      foreach(_configuration IN LISTS _configurations)
         string(TOUPPER "${_configuration}" _configuration)
         list(
            APPEND _properties
            "IMPORTED_IMPLIB_${_configuration}"
            "IMPORTED_LIBNAME_${_configuration}"
            "IMPORTED_LINK_DEPENDENT_LIBRARIES_${_configuration}"
            "IMPORTED_LINK_INTERFACE_LANGUAGES_${_configuration}"
            "IMPORTED_LINK_INTERFACE_LIBRARIES_${_configuration}"
            "IMPORTED_LINK_INTERFACE_MULTIPLICITY_${_configuration}"
            "IMPORTED_LOCATION_${_configuration}"
            "IMPORTED_NO_SONAME_${_configuration}"
            "IMPORTED_OBJECTS_${_configuration}"
            "IMPORTED_SONAME_${_configuration}"
            "MAP_IMPORTED_CONFIG_${_configuration}"
         )
      endforeach()
   endif()
   set(${output} "${_properties}" PARENT_SCOPE)
endfunction()

function(_forge_contract_sealed_source_properties output)
   set(
      _properties
      AUTORCC_OPTIONS
      AUTOUIC_OPTIONS
      COMPILE_DEFINITIONS
      COMPILE_FLAGS
      COMPILE_OPTIONS
      CXX_SCAN_FOR_MODULES
      EXTERNAL_OBJECT
      GENERATED
      HEADER_FILE_ONLY
      INCLUDE_DIRECTORIES
      KEEP_EXTENSION
      LANGUAGE
      MACOSX_PACKAGE_LOCATION
      OBJECT_DEPENDS
      OBJECT_OUTPUTS
      SKIP_AUTOGEN
      SKIP_AUTOMOC
      SKIP_AUTORCC
      SKIP_AUTOUIC
      SKIP_LINTING
      SKIP_PRECOMPILE_HEADERS
      SKIP_UNITY_BUILD_INCLUSION
      SYMBOLIC
      UNITY_GROUP
      VS_COPY_TO_OUT_DIR
      VS_DEPLOYMENT_CONTENT
      VS_DEPLOYMENT_LOCATION
      VS_INCLUDE_IN_VSIX
      VS_RESOURCE_GENERATOR
      VS_SETTINGS
      VS_TOOL_OVERRIDE
      VS_XAML_TYPE
      XCODE_EXPLICIT_FILE_TYPE
      XCODE_FILE_ATTRIBUTES
      XCODE_LAST_KNOWN_FILE_TYPE
   )
   set(
      _build_configurations
      ${CMAKE_BUILD_TYPE}
      ${CMAKE_CONFIGURATION_TYPES}
      DEBUG
      RELEASE
      RELWITHDEBINFO
      MINSIZEREL
   )
   list(REMOVE_DUPLICATES _build_configurations)
   foreach(_configuration IN LISTS _build_configurations)
      string(TOUPPER "${_configuration}" _configuration)
      list(APPEND _properties "COMPILE_DEFINITIONS_${_configuration}")
   endforeach()
   set(${output} "${_properties}" PARENT_SCOPE)
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

function(_forge_contract_hash_fingerprint_records output)
   set(_serialized)
   foreach(_record IN LISTS ARGN)
      string(LENGTH "${_record}" _length)
      string(APPEND _serialized "${_length}:${_record}")
   endforeach()
   string(SHA256 _fingerprint "${_serialized}")
   set(${output} "${_fingerprint}" PARENT_SCOPE)
endfunction()

function(_forge_contract_target_materialization_fingerprint target output)
   get_target_property(_source_root "${target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
   if(NOT _source_root OR _source_root STREQUAL "_source_root-NOTFOUND")
      message(FATAL_ERROR "materialized contract library has no source root: ${target}")
   endif()

   set(_records)
   foreach(
      _property
      IN ITEMS
         FORGE_CONTRACT_DESCRIPTOR_SCHEMA
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
   )
      get_target_property(_value "${target}" "${_property}")
      if(_value STREQUAL "_value-NOTFOUND")
         set(_value)
      endif()
      string(SHA256 _value_hash "${_value}")
      list(APPEND _records "property:${_property}:${_value_hash}")
   endforeach()

   set(_roles module source public_header private_header)
   set(
      _source_properties
      FORGE_CONTRACT_MODULE_SOURCES
      FORGE_CONTRACT_SOURCES
      FORGE_CONTRACT_PUBLIC_HEADERS
      FORGE_CONTRACT_PRIVATE_HEADERS
   )
   foreach(_index RANGE 0 3)
      list(GET _roles ${_index} _role)
      list(GET _source_properties ${_index} _property)
      get_target_property(_logical_paths "${target}" "${_property}")
      if(_logical_paths STREQUAL "_logical_paths-NOTFOUND")
         set(_logical_paths)
      endif()
      foreach(_logical_path IN LISTS _logical_paths)
         set(_physical_path "${_source_root}/${_logical_path}")
         if(NOT EXISTS "${_physical_path}" OR IS_DIRECTORY "${_physical_path}")
            message(
               FATAL_ERROR
               "materialized contract library input is missing: ${_physical_path}"
            )
         endif()
         file(SHA256 "${_physical_path}" _content_hash)
         list(APPEND _records "file:${_role}:${_logical_path}:${_content_hash}")
      endforeach()
   endforeach()

   _forge_contract_hash_fingerprint_records(_fingerprint ${_records})
   set(${output} "${_fingerprint}" PARENT_SCOPE)
endfunction()

function(_forge_contract_declared_source_paths target output)
   get_target_property(_source_root "${target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
   set(_sources)
   if(_source_root AND NOT _source_root STREQUAL "_source_root-NOTFOUND")
      foreach(
         _property
         IN ITEMS
            FORGE_CONTRACT_MODULE_SOURCES
            FORGE_CONTRACT_SOURCES
            FORGE_CONTRACT_PUBLIC_HEADERS
            FORGE_CONTRACT_PRIVATE_HEADERS
      )
         get_target_property(_logical_paths "${target}" "${_property}")
         if(_logical_paths STREQUAL "_logical_paths-NOTFOUND")
            set(_logical_paths)
         endif()
         foreach(_logical_path IN LISTS _logical_paths)
            list(APPEND _sources "${_source_root}/${_logical_path}")
         endforeach()
      endforeach()
   else()
      get_target_property(_imported "${target}" IMPORTED)
      if(NOT _imported)
         set(${output} "" PARENT_SCOPE)
         return()
      endif()
      _forge_contract_imported_location("${target}" _location)
      get_filename_component(_library_directory "${_location}" DIRECTORY)
      get_target_property(
         _module_root_relative "${target}" FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE
      )
      get_target_property(
         _source_root_relative "${target}" FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE
      )
      if(NOT _module_root_relative OR _module_root_relative STREQUAL "_module_root_relative-NOTFOUND")
         message(FATAL_ERROR "imported contract library has no module install root: ${target}")
      endif()
      if(NOT _source_root_relative OR _source_root_relative STREQUAL "_source_root_relative-NOTFOUND")
         message(FATAL_ERROR "imported contract library has no source install root: ${target}")
      endif()
      get_filename_component(
         _installed_module_root "${_library_directory}/${_module_root_relative}" ABSOLUTE
      )
      get_filename_component(
         _installed_source_root "${_library_directory}/${_source_root_relative}" ABSOLUTE
      )
      get_target_property(_module_paths "${target}" FORGE_CONTRACT_INSTALL_MODULE_PATHS)
      get_target_property(
         _public_header_paths "${target}" FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS
      )
      if(_module_paths STREQUAL "_module_paths-NOTFOUND")
         set(_module_paths)
      endif()
      if(_public_header_paths STREQUAL "_public_header_paths-NOTFOUND")
         set(_public_header_paths)
      endif()
      foreach(_logical_path IN LISTS _module_paths _public_header_paths)
         list(APPEND _sources "${_installed_module_root}/${_logical_path}")
      endforeach()
      foreach(_property IN ITEMS FORGE_CONTRACT_SOURCES FORGE_CONTRACT_PRIVATE_HEADERS)
         get_target_property(_logical_paths "${target}" "${_property}")
         if(_logical_paths STREQUAL "_logical_paths-NOTFOUND")
            set(_logical_paths)
         endif()
         foreach(_logical_path IN LISTS _logical_paths)
            list(APPEND _sources "${_installed_source_root}/${_logical_path}")
         endforeach()
      endforeach()
   endif()
   list(REMOVE_DUPLICATES _sources)
   set(${output} "${_sources}" PARENT_SCOPE)
endfunction()

function(_forge_contract_assert_sealed_sources target)
   _forge_contract_declared_source_paths("${target}" _sources)
   _forge_contract_sealed_source_properties(_properties)
   foreach(_source IN LISTS _sources)
      foreach(_property IN LISTS _properties)
         get_property(
            _is_set SOURCE "${_source}" TARGET_DIRECTORY "${target}"
            PROPERTY "${_property}" SET
         )
         if(NOT _is_set)
            continue()
         endif()
         get_property(
            _value SOURCE "${_source}" TARGET_DIRECTORY "${target}"
            PROPERTY "${_property}"
         )
         if(_property STREQUAL "GENERATED" AND NOT _value)
            continue()
         endif()
         if(_property STREQUAL "LANGUAGE" AND (NOT _value OR _value STREQUAL "CXX"))
            continue()
         endif()
         if(
            _property STREQUAL "OBJECT_DEPENDS"
            AND FORGE_CONTRACT_GUEST
            AND "${_value}" STREQUAL "${ForgeContract_ATTR_PLUGIN}"
         )
            continue()
         endif()
         message(
            FATAL_ERROR
            "Forge Contract library source ${_source} uses unsupported source property: "
            "${_property}; declare the input through forge_add_contract_library instead"
         )
      endforeach()
   endforeach()
endfunction()

function(_forge_contract_assert_sealed_target target)
   string(SHA256 _target_key "${target}")
   _forge_contract_sealed_target_properties("${target}" _properties)
   foreach(_property IN LISTS _properties)
      get_property(
         _expected_set GLOBAL
         PROPERTY "FORGE_CONTRACT_SEALED_TARGET_${_target_key}_${_property}_SET"
      )
      get_property(_actual_set TARGET "${target}" PROPERTY "${_property}" SET)
      if(NOT "${_actual_set}" STREQUAL "${_expected_set}")
         message(
            FATAL_ERROR
            "Forge Contract library target ${target} was modified after descriptor "
            "declaration: ${_property}"
         )
      endif()
      if(_actual_set)
         get_property(
            _expected GLOBAL
            PROPERTY "FORGE_CONTRACT_SEALED_TARGET_${_target_key}_${_property}"
         )
         get_property(_actual TARGET "${target}" PROPERTY "${_property}")
         if(NOT "${_actual}" STREQUAL "${_expected}")
            message(
               FATAL_ERROR
               "Forge Contract library target ${target} was modified after descriptor "
               "declaration: ${_property}"
            )
         endif()
      endif()
   endforeach()
   _forge_contract_assert_sealed_sources("${target}")
endfunction()

function(_forge_contract_assert_directory_sealed_targets)
   cmake_language(DEFER GET_CALL_IDS _remaining_calls)
   list(FILTER _remaining_calls EXCLUDE REGEX "^forge_contract_assert_sealed_targets$")
   if(_remaining_calls)
      cmake_language(
         DEFER
         ID forge_contract_assert_sealed_targets
         CALL _forge_contract_assert_directory_sealed_targets
      )
      return()
   endif()
   get_property(_targets DIRECTORY PROPERTY FORGE_CONTRACT_SEALED_TARGETS)
   foreach(_target IN LISTS _targets)
      _forge_contract_assert_sealed_target("${_target}")
   endforeach()
endfunction()

function(_forge_contract_schedule_directory_sealed_target_check target directory)
   get_property(
      _targets DIRECTORY "${directory}" PROPERTY FORGE_CONTRACT_SEALED_TARGETS
   )
   if(NOT target IN_LIST _targets)
      set_property(
         DIRECTORY "${directory}" APPEND PROPERTY FORGE_CONTRACT_SEALED_TARGETS "${target}"
      )
   endif()
   get_property(
      _scheduled DIRECTORY "${directory}"
      PROPERTY FORGE_CONTRACT_SEALED_TARGET_CHECK_SCHEDULED
   )
   if(_scheduled)
      return()
   endif()
   set_property(
      DIRECTORY "${directory}"
      PROPERTY FORGE_CONTRACT_SEALED_TARGET_CHECK_SCHEDULED TRUE
   )
   cmake_language(
      DEFER DIRECTORY "${directory}"
      ID forge_contract_assert_sealed_targets
      CALL _forge_contract_assert_directory_sealed_targets
   )
endfunction()

function(_forge_contract_schedule_sealed_target_check target)
   _forge_contract_schedule_directory_sealed_target_check(
      "${target}" "${CMAKE_CURRENT_BINARY_DIR}"
   )

   get_target_property(_imported "${target}" IMPORTED)
   if(_imported)
      get_target_property(_imported_global "${target}" IMPORTED_GLOBAL)
   endif()
   if(
      (NOT _imported OR _imported_global)
      AND NOT CMAKE_CURRENT_BINARY_DIR STREQUAL CMAKE_BINARY_DIR
   )
      _forge_contract_schedule_directory_sealed_target_check(
         "${target}" "${CMAKE_BINARY_DIR}"
      )
   endif()
endfunction()

function(_forge_contract_require_sealed_target target)
   get_property(_targets GLOBAL PROPERTY FORGE_CONTRACT_SEALED_TARGETS)
   if(NOT target IN_LIST _targets)
      message(
         FATAL_ERROR
         "imported Forge Contract library target ${target} was not registered by its package config"
      )
   endif()
   _forge_contract_assert_sealed_target("${target}")
   _forge_contract_schedule_sealed_target_check("${target}")
endfunction()

function(_forge_contract_seal_target target)
   string(SHA256 _target_key "${target}")
   get_property(_targets GLOBAL PROPERTY FORGE_CONTRACT_SEALED_TARGETS)
   if(NOT target IN_LIST _targets)
      set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_SEALED_TARGETS "${target}")
   endif()
   _forge_contract_sealed_target_properties("${target}" _properties)
   foreach(_property IN LISTS _properties)
      get_property(_is_set TARGET "${target}" PROPERTY "${_property}" SET)
      set_property(
         GLOBAL PROPERTY
         "FORGE_CONTRACT_SEALED_TARGET_${_target_key}_${_property}_SET" "${_is_set}"
      )
      if(_is_set)
         get_property(_value TARGET "${target}" PROPERTY "${_property}")
         set_property(
            GLOBAL PROPERTY
            "FORGE_CONTRACT_SEALED_TARGET_${_target_key}_${_property}" "${_value}"
         )
      endif()
   endforeach()
   _forge_contract_assert_sealed_sources("${target}")
   _forge_contract_schedule_sealed_target_check("${target}")
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
   if(_logical STREQUAL "")
      set(_logical ".")
   endif()
   _forge_contract_reject_unsafe_value("${_absolute}" "${description}")
   _forge_contract_reject_unsafe_value("${_logical}" "${description}")
   set(${output_absolute} "${_absolute}" PARENT_SCOPE)
   set(${output_logical} "${_logical}" PARENT_SCOPE)
endfunction()

function(_forge_contract_path_under_base path bases description output)
   set(_match)
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

function(_forge_contract_configure_guest_target target)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   target_include_directories("${target}" PRIVATE "${ForgeContract_DATA_DIR}/include")
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
         message(FATAL_ERROR "Forge Contract guest component ${ARG_ID} has mismatched module metadata")
      endif()

      _forge_contract_id_key("${ARG_ID}" _key)
      get_property(_existing GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_ID")
      if(_existing)
         message(FATAL_ERROR "duplicate Forge Contract guest component ID: ${ARG_ID}")
      endif()
      set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_ID" "${ARG_ID}")
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_TARGET"
         "${ARG_TARGET}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_MODULE_NAMES"
         "${ARG_MODULE_NAMES}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_DEPENDENCIES"
         "${ARG_PUBLIC_LIBRARIES}"
      )
      if(ARG_FOUNDATION)
         set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_FOUNDATION_COMPONENT_IDS "${ARG_ID}")
      endif()

      if(NOT FORGE_CONTRACT_GUEST)
         return()
      endif()
      if(TARGET "Forge::${ARG_TARGET}")
         message(FATAL_ERROR "Forge Contract guest target already exists: Forge::${ARG_TARGET}")
      endif()

      string(SUBSTRING "${_key}" 0 12 _short_key)
      set(_target "_forge_contract_guest_${ARG_TARGET}_${_short_key}")
      set(_module_paths)
      foreach(_module IN LISTS ARG_MODULES)
         set(_path "${ForgeContract_DATA_DIR}/modules/${_module}")
         if(NOT EXISTS "${_path}")
            message(FATAL_ERROR "Forge Contract guest component module is missing: ${_path}")
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
         set(_archive_path "${ForgeContract_SYSROOT}/lib/${ARG_ARCHIVE}")
         if(NOT EXISTS "${_archive_path}")
            message(FATAL_ERROR "Forge Contract guest component archive is missing: ${_archive_path}")
         endif()
         set(_archive_target "${_target}_archive")
         add_library("${_archive_target}" STATIC IMPORTED GLOBAL)
         set_target_properties(
            "${_archive_target}" PROPERTIES IMPORTED_LOCATION "${_archive_path}"
         )
         target_link_libraries("${_target}" PUBLIC "${_archive_target}")
      endif()
      target_compile_features("${_target}" PUBLIC cxx_std_23)
      set_target_properties(
         "${_target}"
         PROPERTIES
            CXX_MODULE_STD OFF
            CXX_SCAN_FOR_MODULES ON
            FORGE_CONTRACT_GUEST_COMPONENT_ID "${ARG_ID}"
            FORGE_CONTRACT_GUEST_MODULE_NAMES "${ARG_MODULE_NAMES}"
            FORGE_CONTRACT_GUEST_PUBLIC_COMPONENT_IDS "${ARG_PUBLIC_LIBRARIES}"
      )
      _forge_contract_configure_guest_target("${_target}")
      set_property(GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_key}" "${_target}")
      set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS "${ARG_ID}")
   endfunction()
endif()

function(_forge_contract_component_descriptor id modules dependencies target_name)
   _forge_contract_id_key("${id}" _key)
   get_property(_registered GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_ID")
   get_property(
      _modules GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_MODULE_NAMES"
   )
   get_property(
      _dependencies GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_DEPENDENCIES"
   )
   get_property(
      _target_name GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_TARGET"
   )
   if(NOT _registered STREQUAL id OR NOT _modules OR NOT _target_name)
      message(FATAL_ERROR "unknown Forge Contract guest component descriptor: ${id}")
   endif()
   set(${modules} "${_modules}" PARENT_SCOPE)
   set(${dependencies} "${_dependencies}" PARENT_SCOPE)
   set(${target_name} "${_target_name}" PARENT_SCOPE)
endfunction()

function(_forge_contract_finalize_guest_components)
   if(NOT FORGE_CONTRACT_GUEST)
      return()
   endif()
   get_property(_ids GLOBAL PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS)
   foreach(_id IN LISTS _ids)
      _forge_contract_id_key("${_id}" _key)
      get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_key}")
      get_property(
         _dependencies GLOBAL PROPERTY
         "FORGE_CONTRACT_GUEST_DESCRIPTOR_${_key}_DEPENDENCIES"
      )
      foreach(_dependency IN LISTS _dependencies)
         _forge_contract_id_key("${_dependency}" _dependency_key)
         get_property(
            _dependency_target GLOBAL PROPERTY
            "FORGE_CONTRACT_COMPONENT_TARGET_${_dependency_key}"
         )
         if(NOT _dependency_target)
            message(FATAL_ERROR "Forge Contract guest component ${_id} has unknown dependency ${_dependency}")
         endif()
         target_link_libraries("${_target}" PUBLIC "${_dependency_target}")
      endforeach()
   endforeach()

   _forge_contract_id_key("forge.contract.runtime" _runtime_key)
   get_property(
      _runtime_target GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_runtime_key}"
   )
   if(NOT _runtime_target)
      message(FATAL_ERROR "Forge Contract SDK has no guest runtime component")
   endif()
   if(NOT TARGET _forge_contract_guest_eosio)
      add_library(_forge_contract_guest_eosio INTERFACE)
      target_include_directories(
         _forge_contract_guest_eosio INTERFACE "${ForgeContract_DATA_DIR}/include"
      )
      target_link_libraries(_forge_contract_guest_eosio INTERFACE "${_runtime_target}")
   endif()
   set_property(
      GLOBAL PROPERTY FORGE_CONTRACT_GUEST_EOSIO_TARGET _forge_contract_guest_eosio
   )
endfunction()

function(_forge_contract_register_library_target target)
   get_target_property(_id "${target}" FORGE_CONTRACT_LIBRARY_ID)
   if(NOT _id)
      message(FATAL_ERROR "contract library target has no stable ID: ${target}")
   endif()
   _forge_contract_require_sealed_target("${target}")
   _forge_contract_id_key("${_id}" _key)
   get_property(_component GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_key}")
   if(_component)
      message(FATAL_ERROR "Forge Contract graph ID is shared by a library and component: ${_id}")
   endif()
   get_property(_registered GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}")
   if(_registered AND NOT _registered STREQUAL target)
      get_target_property(
         _registered_fingerprint "${_registered}" FORGE_CONTRACT_PACKAGE_FINGERPRINT
      )
      get_target_property(
         _target_fingerprint "${target}" FORGE_CONTRACT_PACKAGE_FINGERPRINT
      )
      if(
         NOT _registered_fingerprint
         OR _registered_fingerprint STREQUAL "_registered_fingerprint-NOTFOUND"
         OR NOT _target_fingerprint
         OR _target_fingerprint STREQUAL "_target_fingerprint-NOTFOUND"
         OR NOT "${_registered_fingerprint}" STREQUAL "${_target_fingerprint}"
      )
         message(FATAL_ERROR "conflicting Forge Contract library ID: ${_id}")
      endif()
      return()
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}" "${target}")
endfunction()

function(forge_register_contract_library_targets)
   if(NOT ARGN)
      message(FATAL_ERROR "forge_register_contract_library_targets requires imported targets")
   endif()
   foreach(_input IN LISTS ARGN)
      _forge_contract_resolve_target("${_input}" _target)
      get_target_property(_imported "${_target}" IMPORTED)
      get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
      if(NOT _imported OR NOT _contract_library)
         message(
            FATAL_ERROR
            "forge_register_contract_library_targets requires an imported "
            "Forge Contract library target: ${_input}"
         )
      endif()
      get_property(_sealed GLOBAL PROPERTY FORGE_CONTRACT_SEALED_TARGETS)
      if(_target IN_LIST _sealed)
         _forge_contract_assert_sealed_target("${_target}")
      else()
         _forge_contract_seal_target("${_target}")
      endif()
      _forge_contract_register_library_target("${_target}")
   endforeach()
endfunction()

function(_forge_contract_package_target_materialized output)
   cmake_parse_arguments(
      ARG
      ""
      "PACKAGE;TARGET;ID;FINGERPRINT"
      ""
      ${ARGN}
   )
   if(
      ARG_UNPARSED_ARGUMENTS
      OR NOT ARG_PACKAGE
      OR NOT ARG_TARGET
      OR NOT ARG_ID
      OR NOT ARG_FINGERPRINT
   )
      message(FATAL_ERROR "invalid installed Forge Contract package descriptor")
   endif()
   if(TARGET "${ARG_TARGET}")
      _forge_contract_resolve_target("${ARG_TARGET}" _target)
      get_target_property(_contract_library "${_target}" FORGE_CONTRACT_LIBRARY)
      get_target_property(_actual_id "${_target}" FORGE_CONTRACT_LIBRARY_ID)
      if(NOT _contract_library OR NOT "${_actual_id}" STREQUAL "${ARG_ID}")
         message(
            FATAL_ERROR
            "existing target does not match installed Forge Contract package "
            "${ARG_PACKAGE}: ${ARG_TARGET}"
         )
      endif()
      get_target_property(
         _actual_fingerprint "${_target}" FORGE_CONTRACT_PACKAGE_FINGERPRINT
      )
      if(NOT "${_actual_fingerprint}" STREQUAL "${ARG_FINGERPRINT}")
         message(
            FATAL_ERROR
            "conflicting installed Forge Contract package descriptor "
            "${ARG_PACKAGE}: ${ARG_ID}"
         )
      endif()
      _forge_contract_register_library_target("${_target}")
      set(${output} TRUE PARENT_SCOPE)
      return()
   endif()

   _forge_contract_id_key("${ARG_ID}" _key)
   get_property(
      _registered GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}"
   )
   if(NOT _registered)
      set(${output} FALSE PARENT_SCOPE)
      return()
   endif()

   _forge_contract_require_sealed_target("${_registered}")
   get_target_property(
      _registered_fingerprint "${_registered}" FORGE_CONTRACT_PACKAGE_FINGERPRINT
   )
   if(NOT "${_registered_fingerprint}" STREQUAL "${ARG_FINGERPRINT}")
      message(
         FATAL_ERROR
         "conflicting installed Forge Contract package descriptor "
         "${ARG_PACKAGE}: ${ARG_ID}"
      )
   endif()
   add_library("${ARG_TARGET}" ALIAS "${_registered}")
   set(${output} TRUE PARENT_SCOPE)
endfunction()

function(_forge_contract_bind_package_target)
   cmake_parse_arguments(
      ARG
      ""
      "PACKAGE;TARGET;ID;FINGERPRINT"
      ""
      ${ARGN}
   )
   if(
      ARG_UNPARSED_ARGUMENTS
      OR NOT ARG_PACKAGE
      OR NOT ARG_TARGET
      OR NOT ARG_ID
      OR NOT ARG_FINGERPRINT
   )
      message(FATAL_ERROR "invalid installed Forge Contract package binding")
   endif()

   _forge_contract_resolve_target("${ARG_TARGET}" _target)
   get_target_property(_actual_id "${_target}" FORGE_CONTRACT_LIBRARY_ID)
   if(NOT "${_actual_id}" STREQUAL "${ARG_ID}")
      message(
         FATAL_ERROR
         "installed Forge Contract package target has the wrong ID "
         "${ARG_PACKAGE}: ${ARG_TARGET}"
      )
   endif()
   _forge_contract_target_materialization_fingerprint("${_target}" _actual_fingerprint)
   if(NOT "${_actual_fingerprint}" STREQUAL "${ARG_FINGERPRINT}")
      message(
         FATAL_ERROR
         "conflicting installed Forge Contract package descriptor "
         "${ARG_PACKAGE}: ${ARG_ID}"
      )
   endif()

   _forge_contract_assert_sealed_target("${_target}")
   set_property(
      TARGET "${_target}"
      PROPERTY FORGE_CONTRACT_PACKAGE_FINGERPRINT "${ARG_FINGERPRINT}"
   )
   _forge_contract_seal_target("${_target}")
   _forge_contract_register_library_target("${_target}")
endfunction()

function(_forge_contract_register_component_target target)
   get_target_property(_id "${target}" FORGE_CONTRACT_GUEST_COMPONENT_ID)
   if(NOT _id)
      message(FATAL_ERROR "Forge guest component target has no stable ID: ${target}")
   endif()
   _forge_contract_id_key("${_id}" _key)
   get_property(_library GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}")
   if(_library)
      message(FATAL_ERROR "Forge Contract graph ID is shared by a library and component: ${_id}")
   endif()
   get_property(_registered GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_key}")
   if(_registered AND NOT _registered STREQUAL target)
      message(FATAL_ERROR "duplicate Forge guest component ID: ${_id}")
   endif()
   _forge_contract_component_descriptor(
      "${_id}" _descriptor_modules _descriptor_dependencies _descriptor_target
   )
   get_target_property(_target_modules "${target}" FORGE_CONTRACT_GUEST_MODULE_NAMES)
   get_target_property(
      _target_dependencies "${target}" FORGE_CONTRACT_GUEST_PUBLIC_COMPONENT_IDS
   )
   if(_target_dependencies STREQUAL "_target_dependencies-NOTFOUND")
      set(_target_dependencies)
   endif()
   if(
      NOT "${_target_modules}" STREQUAL "${_descriptor_modules}"
      OR NOT "${_target_dependencies}" STREQUAL "${_descriptor_dependencies}"
   )
      message(FATAL_ERROR "Forge guest component target does not match SDK descriptor: ${_id}")
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_key}" "${target}")
endfunction()

function(_forge_contract_find_library_target id output)
   _forge_contract_id_key("${id}" _key)
   get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_TARGET_${_key}")
   if(NOT _target)
      message(FATAL_ERROR "contract library dependency ID is not visible: ${id}")
   endif()
   set(${output} "${_target}" PARENT_SCOPE)
endfunction()

function(_forge_contract_find_component_target id output)
   _forge_contract_id_key("${id}" _key)
   get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_COMPONENT_TARGET_${_key}")
   if(NOT _target)
      message(FATAL_ERROR "Forge guest component ID is not visible: ${id}")
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
      _forge_contract_register_component_target("${_target}")
   else()
      message(FATAL_ERROR "contract dependency is not guest-compatible: ${input}")
   endif()
   set(${kind_output} "${_kind}" PARENT_SCOPE)
   set(${id_output} "${_id}" PARENT_SCOPE)
   set(${target_output} "${_target}" PARENT_SCOPE)
endfunction()

function(_forge_contract_dependency_ids dependencies scope library_ids component_ids targets)
   set(_library_ids)
   set(_component_ids)
   set(_targets)
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
      list(APPEND _targets "${_dependency}")
   endforeach()
   set(${library_ids} "${_library_ids}" PARENT_SCOPE)
   set(${component_ids} "${_component_ids}" PARENT_SCOPE)
   set(${targets} "${_targets}" PARENT_SCOPE)
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
   set(_concrete "_forge_contract_library_${_short_id}")
   string(REGEX REPLACE "^.*::" "" _output_name "${target}")
   add_library("${_concrete}" STATIC)
   add_library("${target}" ALIAS "${_concrete}")
   set_target_properties(
      "${_concrete}"
      PROPERTIES
         CXX_MODULE_STD OFF
         CXX_SCAN_FOR_MODULES ON
         LINK_LIBRARIES_ONLY_TARGETS ON
         LINK_LIBRARIES_STRATEGY REORDER_MINIMALLY
         OUTPUT_NAME "${_output_name}"
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
   foreach(_module_base IN LISTS _module_bases_absolute)
      target_include_directories(
         "${_concrete}"
         PUBLIC "$<BUILD_INTERFACE:${_module_base}>"
      )
   endforeach()
   if(_public_targets)
      target_link_libraries("${_concrete}" PUBLIC ${_public_targets})
   endif()
   if(_private_targets)
      target_link_libraries("${_concrete}" PRIVATE ${_private_targets})
   endif()
   if(FORGE_CONTRACT_GUEST)
      set_source_files_properties(
         ${_MODULE_SOURCES_ABSOLUTE} ${_SOURCES_ABSOLUTE}
         PROPERTIES OBJECT_DEPENDS "${ForgeContract_ATTR_PLUGIN}"
      )
      _forge_contract_configure_guest_target("${_concrete}")
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
         FORGE_CONTRACT_PUBLIC_LIBRARY_TARGETS "${ARG_PUBLIC_LIBRARIES}"
         FORGE_CONTRACT_PRIVATE_LIBRARY_TARGETS "${ARG_PRIVATE_LIBRARIES}"
         FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE ""
         FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE ""
         FORGE_CONTRACT_INSTALL_MODULE_PATHS ""
         FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS ""
   )
   _forge_contract_seal_target("${_concrete}")
   _forge_contract_register_library_target("${_concrete}")
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
   _forge_contract_assert_sealed_target("${_target}")
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
         FORGE_CONTRACT_INSTALL_MODULE_DESTINATION "${ARG_MODULE_DESTINATION}"
         FORGE_CONTRACT_INSTALL_SOURCE_DESTINATION "${ARG_SOURCE_DESTINATION}"
         EXPORT_PROPERTIES "${_FORGE_CONTRACT_EXPORTED_PROPERTIES}"
   )
   target_include_directories(
      "${_target}"
      PUBLIC "$<INSTALL_INTERFACE:${ARG_MODULE_DESTINATION}>"
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
   _forge_contract_seal_target("${_target}")
endfunction()

function(_forge_contract_cmake_quote value output)
   string(REPLACE "\\" "\\\\" _escaped "${value}")
   string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
   set(${output} "\"${_escaped}\"" PARENT_SCOPE)
endfunction()

function(_forge_contract_install_materialization_fingerprint target output)
   get_target_property(_schema "${target}" FORGE_CONTRACT_DESCRIPTOR_SCHEMA)
   get_target_property(_id "${target}" FORGE_CONTRACT_LIBRARY_ID)
   get_target_property(_source_root "${target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
   get_target_property(
      _module_destination "${target}" FORGE_CONTRACT_INSTALL_MODULE_DESTINATION
   )
   get_target_property(
      _source_destination "${target}" FORGE_CONTRACT_INSTALL_SOURCE_DESTINATION
   )
   get_target_property(_module_sources "${target}" FORGE_CONTRACT_MODULE_SOURCES)
   get_target_property(_sources "${target}" FORGE_CONTRACT_SOURCES)
   get_target_property(_public_headers "${target}" FORGE_CONTRACT_PUBLIC_HEADERS)
   get_target_property(_private_headers "${target}" FORGE_CONTRACT_PRIVATE_HEADERS)
   get_target_property(_module_paths "${target}" FORGE_CONTRACT_INSTALL_MODULE_PATHS)
   get_target_property(
      _public_header_paths "${target}" FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS
   )
   foreach(
      _property
      IN ITEMS
         FORGE_CONTRACT_PUBLIC_LIBRARY_IDS
         FORGE_CONTRACT_PRIVATE_LIBRARY_IDS
         FORGE_CONTRACT_PUBLIC_COMPONENT_IDS
         FORGE_CONTRACT_PRIVATE_COMPONENT_IDS
   )
      get_target_property(_value "${target}" "${_property}")
      if(_value STREQUAL "_value-NOTFOUND")
         set(_value)
      endif()
      set("_${_property}" "${_value}")
   endforeach()

   set(_materialized_module_bases "${_module_destination}")
   set(_materialized_module_sources)
   foreach(_path IN LISTS _module_paths)
      list(APPEND _materialized_module_sources "${_module_destination}/${_path}")
   endforeach()
   set(_materialized_sources)
   foreach(_path IN LISTS _sources)
      list(APPEND _materialized_sources "${_source_destination}/${_path}")
   endforeach()
   set(_materialized_public_headers)
   foreach(_path IN LISTS _public_header_paths)
      list(APPEND _materialized_public_headers "${_module_destination}/${_path}")
   endforeach()
   set(_materialized_private_headers)
   foreach(_path IN LISTS _private_headers)
      list(APPEND _materialized_private_headers "${_source_destination}/${_path}")
   endforeach()

   set(
      _metadata_properties
      FORGE_CONTRACT_DESCRIPTOR_SCHEMA
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
   )
   set(
      _metadata_variables
      _schema
      _id
      _materialized_module_bases
      _materialized_module_sources
      _materialized_sources
      _materialized_public_headers
      _materialized_private_headers
      _FORGE_CONTRACT_PUBLIC_LIBRARY_IDS
      _FORGE_CONTRACT_PRIVATE_LIBRARY_IDS
      _FORGE_CONTRACT_PUBLIC_COMPONENT_IDS
      _FORGE_CONTRACT_PRIVATE_COMPONENT_IDS
   )
   set(_records)
   foreach(_index RANGE 0 10)
      list(GET _metadata_properties ${_index} _property)
      list(GET _metadata_variables ${_index} _variable)
      set(_value "${${_variable}}")
      string(SHA256 _value_hash "${_value}")
      list(APPEND _records "property:${_property}:${_value_hash}")
   endforeach()

   set(
      _roles
      module
      source
      public_header
      private_header
   )
   set(
      _original_path_variables
      _module_sources
      _sources
      _public_headers
      _private_headers
   )
   set(
      _materialized_path_variables
      _materialized_module_sources
      _materialized_sources
      _materialized_public_headers
      _materialized_private_headers
   )
   foreach(_role_index RANGE 0 3)
      list(GET _roles ${_role_index} _role)
      list(GET _original_path_variables ${_role_index} _original_variable)
      list(GET _materialized_path_variables ${_role_index} _materialized_variable)
      set(_original_paths "${${_original_variable}}")
      set(_materialized_paths "${${_materialized_variable}}")
      list(LENGTH _original_paths _original_count)
      list(LENGTH _materialized_paths _materialized_count)
      if(NOT _original_count EQUAL _materialized_count)
         message(
            FATAL_ERROR
            "installed Forge Contract package has inconsistent ${_role} paths: ${target}"
         )
      endif()
      if(_original_count EQUAL 0)
         continue()
      endif()
      math(EXPR _last "${_original_count} - 1")
      foreach(_index RANGE 0 ${_last})
         list(GET _original_paths ${_index} _original_path)
         list(GET _materialized_paths ${_index} _materialized_path)
         set(_physical_path "${_source_root}/${_original_path}")
         if(NOT EXISTS "${_physical_path}" OR IS_DIRECTORY "${_physical_path}")
            message(
               FATAL_ERROR
               "installed Forge Contract package input is missing: ${_physical_path}"
            )
         endif()
         file(SHA256 "${_physical_path}" _content_hash)
         list(
            APPEND _records
            "file:${_role}:${_materialized_path}:${_content_hash}"
         )
      endforeach()
   endforeach()

   _forge_contract_hash_fingerprint_records(_fingerprint ${_records})
   set(${output} "${_fingerprint}" PARENT_SCOPE)
endfunction()

function(_forge_contract_assign_package_fingerprint target fingerprint)
   _forge_contract_assert_sealed_target("${target}")
   get_target_property(
      _existing "${target}" FORGE_CONTRACT_PACKAGE_FINGERPRINT
   )
   if(_existing AND NOT _existing STREQUAL "_existing-NOTFOUND")
      if(NOT "${_existing}" STREQUAL "${fingerprint}")
         message(
            FATAL_ERROR
            "contract library is assigned conflicting package fingerprints: ${target}"
         )
      endif()
      return()
   endif()
   set_property(
      TARGET "${target}"
      PROPERTY FORGE_CONTRACT_PACKAGE_FINGERPRINT "${fingerprint}"
   )
   _forge_contract_seal_target("${target}")
endfunction()

function(forge_install_contract_package)
   cmake_parse_arguments(
      ARG
      ""
      "EXPORT;FILE;NAMESPACE;DESTINATION"
      "TARGETS"
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "forge_install_contract_package received unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
   endif()
   foreach(_required EXPORT FILE NAMESPACE DESTINATION TARGETS)
      if(NOT ARG_${_required})
         message(FATAL_ERROR "forge_install_contract_package requires ${_required}")
      endif()
   endforeach()
   if(IS_ABSOLUTE "${ARG_DESTINATION}" OR NOT ARG_FILE MATCHES "\\.cmake$")
      message(FATAL_ERROR "contract package destination and file must be relative CMake paths")
   endif()

   set(_package_ids)
   set(_package_names)
   set(_package_targets)
   foreach(_input IN LISTS ARG_TARGETS)
      _forge_contract_resolve_target("${_input}" _target)
      get_target_property(_id "${_target}" FORGE_CONTRACT_LIBRARY_ID)
      get_target_property(_module_destination "${_target}" FORGE_CONTRACT_INSTALL_MODULE_DESTINATION)
      get_target_property(_source_destination "${_target}" FORGE_CONTRACT_INSTALL_SOURCE_DESTINATION)
      if(NOT _id OR NOT _module_destination OR NOT _source_destination)
         message(FATAL_ERROR "contract package target was not installed through Forge: ${_input}")
      endif()
      _forge_contract_install_materialization_fingerprint(
         "${_target}" _package_fingerprint
      )
      _forge_contract_assign_package_fingerprint(
         "${_target}" "${_package_fingerprint}"
      )
      get_target_property(_export_name "${_target}" EXPORT_NAME)
      if(NOT _export_name)
         set(_export_name "${_input}")
      endif()
      list(APPEND _package_ids "${_id}")
      list(APPEND _package_names "${ARG_NAMESPACE}${_export_name}")
      list(APPEND _package_targets "${_target}")
   endforeach()

   string(SHA256 _package_graph_key "${CMAKE_CURRENT_BINARY_DIR}/${ARG_FILE}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_package_graph_key}_NODES" "")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_package_graph_key}_COMPONENT_NODES" "")
   foreach(_id IN LISTS _package_ids)
      _forge_contract_collect_library("${_package_graph_key}" "${_id}")
   endforeach()
   get_property(
      _ordered_package_ids GLOBAL PROPERTY
      "FORGE_CONTRACT_GRAPH_${_package_graph_key}_NODES"
   )
   set(_ordered_package_names)
   set(_ordered_package_targets)
   foreach(_id IN LISTS _ordered_package_ids)
      list(FIND _package_ids "${_id}" _package_index)
      if(_package_index EQUAL -1)
         message(FATAL_ERROR "contract package omits transitive dependency ${_id}")
      endif()
      list(GET _package_names ${_package_index} _name)
      list(GET _package_targets ${_package_index} _target)
      list(APPEND _ordered_package_names "${_name}")
      list(APPEND _ordered_package_targets "${_target}")
   endforeach()
   set(_package_ids "${_ordered_package_ids}")
   set(_package_names "${_ordered_package_names}")
   set(_package_targets "${_ordered_package_targets}")

   set(_output "${CMAKE_CURRENT_BINARY_DIR}/${ARG_FILE}")
   file(
      WRITE "${_output}"
      "if(FORGE_CONTRACT_GUEST)\n"
      "   get_filename_component(_forge_contract_package_prefix\n"
      "      \"\${CMAKE_CURRENT_LIST_DIR}/"
   )
   set(_anchor "/__forge_contract_prefix")
   file(
      RELATIVE_PATH _prefix_relative
      "${_anchor}/${ARG_DESTINATION}"
      "${_anchor}"
   )
   file(APPEND "${_output}" "${_prefix_relative}\" ABSOLUTE)\n")

   list(LENGTH _package_targets _target_count)
   math(EXPR _target_last "${_target_count} - 1")
   foreach(_index RANGE 0 ${_target_last})
      list(GET _package_targets ${_index} _target)
      list(GET _package_names ${_index} _package_name)
      get_target_property(_id "${_target}" FORGE_CONTRACT_LIBRARY_ID)
      get_target_property(_module_destination "${_target}" FORGE_CONTRACT_INSTALL_MODULE_DESTINATION)
      get_target_property(_source_destination "${_target}" FORGE_CONTRACT_INSTALL_SOURCE_DESTINATION)
      get_target_property(_module_paths "${_target}" FORGE_CONTRACT_INSTALL_MODULE_PATHS)
      get_target_property(_public_header_paths "${_target}" FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS)
      get_target_property(_sources "${_target}" FORGE_CONTRACT_SOURCES)
      get_target_property(_private_headers "${_target}" FORGE_CONTRACT_PRIVATE_HEADERS)
      get_target_property(_public_library_ids "${_target}" FORGE_CONTRACT_PUBLIC_LIBRARY_IDS)
      get_target_property(_private_library_ids "${_target}" FORGE_CONTRACT_PRIVATE_LIBRARY_IDS)
      get_target_property(_public_component_ids "${_target}" FORGE_CONTRACT_PUBLIC_COMPONENT_IDS)
      get_target_property(_private_component_ids "${_target}" FORGE_CONTRACT_PRIVATE_COMPONENT_IDS)
      get_target_property(
         _package_fingerprint "${_target}" FORGE_CONTRACT_PACKAGE_FINGERPRINT
      )

      set(_public_dependencies)
      set(_private_dependencies)
      foreach(_scope PUBLIC PRIVATE)
         string(TOLOWER "${_scope}" _scope_lower)
         set(_dependencies)
         foreach(_dependency_id IN LISTS _${_scope_lower}_library_ids)
            list(FIND _package_ids "${_dependency_id}" _dependency_index)
            if(_dependency_index EQUAL -1)
               message(FATAL_ERROR "contract package omits dependency ${_dependency_id} of ${_id}")
            endif()
            list(GET _package_names ${_dependency_index} _dependency_name)
            list(APPEND _dependencies "${_dependency_name}")
         endforeach()
         foreach(_component_id IN LISTS _${_scope_lower}_component_ids)
            _forge_contract_component_descriptor(
               "${_component_id}" _unused_modules _unused_dependencies _component_target
            )
            list(APPEND _dependencies "Forge::${_component_target}")
         endforeach()
         set(_${_scope}_dependencies "${_dependencies}")
      endforeach()

      file(APPEND "${_output}" "   _forge_contract_package_target_materialized(\n")
      file(APPEND "${_output}" "      _forge_contract_package_target_ready\n")
      file(APPEND "${_output}" "      PACKAGE \"${ARG_FILE}\"\n")
      file(APPEND "${_output}" "      TARGET ${_package_name}\n")
      file(APPEND "${_output}" "      ID ${_id}\n")
      file(APPEND "${_output}" "      FINGERPRINT ${_package_fingerprint}\n")
      file(APPEND "${_output}" "   )\n")
      file(APPEND "${_output}" "   if(NOT _forge_contract_package_target_ready)\n")
      file(
         APPEND "${_output}"
         "      forge_add_contract_library(${_package_name}\n"
         "         ID ${_id}\n"
         "         SOURCE_ROOT \"\${_forge_contract_package_prefix}\"\n"
         "         MODULE_BASE_DIRS \"\${_forge_contract_package_prefix}/${_module_destination}\"\n"
         "         MODULE_SOURCES\n"
      )
      foreach(_path IN LISTS _module_paths)
         file(APPEND "${_output}" "            \"\${_forge_contract_package_prefix}/${_module_destination}/${_path}\"\n")
      endforeach()
      if(_sources)
         file(APPEND "${_output}" "         SOURCES\n")
         foreach(_path IN LISTS _sources)
            file(APPEND "${_output}" "            \"\${_forge_contract_package_prefix}/${_source_destination}/${_path}\"\n")
         endforeach()
      endif()
      if(_public_header_paths)
         file(APPEND "${_output}" "         PUBLIC_HEADERS\n")
         foreach(_path IN LISTS _public_header_paths)
            file(APPEND "${_output}" "            \"\${_forge_contract_package_prefix}/${_module_destination}/${_path}\"\n")
         endforeach()
      endif()
      if(_private_headers)
         file(APPEND "${_output}" "         PRIVATE_HEADERS\n")
         foreach(_path IN LISTS _private_headers)
            file(APPEND "${_output}" "            \"\${_forge_contract_package_prefix}/${_source_destination}/${_path}\"\n")
         endforeach()
      endif()
      foreach(_scope PUBLIC PRIVATE)
         if(_${_scope}_dependencies)
            file(APPEND "${_output}" "         ${_scope}_LIBRARIES\n")
            foreach(_dependency IN LISTS _${_scope}_dependencies)
               file(APPEND "${_output}" "            ${_dependency}\n")
            endforeach()
         endif()
      endforeach()
      file(APPEND "${_output}" "      )\n")
      file(APPEND "${_output}" "      _forge_contract_bind_package_target(\n")
      file(APPEND "${_output}" "         PACKAGE \"${ARG_FILE}\"\n")
      file(APPEND "${_output}" "         TARGET ${_package_name}\n")
      file(APPEND "${_output}" "         ID ${_id}\n")
      file(APPEND "${_output}" "         FINGERPRINT ${_package_fingerprint}\n")
      file(APPEND "${_output}" "      )\n")
      file(APPEND "${_output}" "   endif()\n")
   endforeach()
   file(APPEND "${_output}" "   unset(_forge_contract_package_target_ready)\n")
   file(APPEND "${_output}" "else()\n")
   file(APPEND "${_output}" "   include(\"\${CMAKE_CURRENT_LIST_DIR}/${ARG_EXPORT}.cmake\")\n")
   file(APPEND "${_output}" "   forge_register_contract_library_targets(\n")
   foreach(_package_name IN LISTS _package_names)
      file(APPEND "${_output}" "      ${_package_name}\n")
   endforeach()
   file(APPEND "${_output}" "   )\nendif()\n")
   install(FILES "${_output}" DESTINATION "${ARG_DESTINATION}")
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

function(_forge_contract_collect_component graph id)
   _forge_contract_id_key("${id}" _key)
   get_property(_state GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_COMPONENT_${_key}_STATE")
   if(_state STREQUAL "visited")
      return()
   endif()
   if(_state STREQUAL "visiting")
      message(FATAL_ERROR "cycle in Forge guest component dependencies at ${id}")
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_COMPONENT_${_key}_STATE" visiting)
   _forge_contract_component_descriptor("${id}" _modules _dependencies _target_name)
   foreach(_dependency IN LISTS _dependencies)
      _forge_contract_collect_component("${graph}" "${_dependency}")
   endforeach()
   set_property(GLOBAL APPEND PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_COMPONENT_NODES" "${id}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_COMPONENT_${_key}_STATE" visited)
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
   foreach(_property FORGE_CONTRACT_PUBLIC_COMPONENT_IDS FORGE_CONTRACT_PRIVATE_COMPONENT_IDS)
      get_target_property(_dependencies "${_target}" "${_property}")
      if(_dependencies STREQUAL "_dependencies-NOTFOUND")
         set(_dependencies)
      endif()
      foreach(_dependency IN LISTS _dependencies)
         _forge_contract_collect_component("${graph}" "${_dependency}")
      endforeach()
   endforeach()
   set_property(GLOBAL APPEND PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_NODES" "${id}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${graph}_${_key}_STATE" visited)
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

function(_forge_contract_write_graph)
   cmake_parse_arguments(
      ARG
      ""
      "TARGET;CONTRACT;SOURCE_ROOT;DISPATCH_SOURCE;RICARDIAN_CONTRACTS;RICARDIAN_CONTRACTS_LOGICAL;RICARDIAN_CLAUSES;RICARDIAN_CLAUSES_LOGICAL;OUTPUT_FILE;OUTPUT_HASH;BUILD_DEPENDENCIES;LIBRARY_TARGETS;COMPONENT_TARGETS;MODULE_BASES;SOURCE_INPUTS"
      "SOURCES;SOURCE_LOGICAL;HEADERS;HEADER_LOGICAL;COMPILE_CHECKS;COMPILE_CHECK_LOGICAL;LIBRARIES"
      ${ARGN}
   )
   foreach(_required TARGET CONTRACT SOURCE_ROOT DISPATCH_SOURCE OUTPUT_FILE OUTPUT_HASH BUILD_DEPENDENCIES)
      if(NOT ARG_${_required})
         message(FATAL_ERROR "_forge_contract_write_graph requires ${_required}")
      endif()
   endforeach()

   string(SHA256 _graph_key "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph_key}_NODES" "")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph_key}_COMPONENT_NODES" "")

   set(_root_library_ids)
   set(_root_component_ids)
   set(_root_targets)
   get_property(_foundation_component_ids GLOBAL PROPERTY FORGE_CONTRACT_FOUNDATION_COMPONENT_IDS)
   list(REMOVE_DUPLICATES _foundation_component_ids)
   foreach(_id IN LISTS _foundation_component_ids)
      list(APPEND _root_component_ids "${_id}")
      _forge_contract_collect_component("${_graph_key}" "${_id}")
   endforeach()
   foreach(_dependency IN LISTS ARG_LIBRARIES)
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
         _forge_contract_collect_component("${_graph_key}" "${_id}")
      endif()
      list(APPEND _root_targets "${_dependency_target}")
   endforeach()

   get_property(_component_ids GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph_key}_COMPONENT_NODES")
   set(_components "[]")
   set(_component_targets)
   set(_component_index 0)
   foreach(_id IN LISTS _component_ids)
      _forge_contract_component_descriptor("${_id}" _module_names _dependencies _target_name)
      _forge_contract_find_component_target("${_id}" _component_target)
      list(APPEND _component_targets "${_component_target}")
      _forge_contract_json_quote("${_id}" _quoted_id)
      _forge_contract_json_array("${_module_names}" _module_names_json)
      _forge_contract_json_array("${_dependencies}" _dependencies_json)
      set(_component "{\"id\":${_quoted_id},\"modules\":${_module_names_json},\"dependencies\":${_dependencies_json}}")
      string(JSON _components SET "${_components}" ${_component_index} "${_component}")
      math(EXPR _component_index "${_component_index} + 1")
   endforeach()

   get_property(_node_ids GLOBAL PROPERTY "FORGE_CONTRACT_GRAPH_${_graph_key}_NODES")
   set(_nodes "[]")
   set(_library_targets)
   set(_module_bases)
   set(_source_inputs)
   set(_node_index 0)
   foreach(_id IN LISTS _node_ids)
      _forge_contract_find_library_target("${_id}" _node_target)
      get_target_property(_source_root "${_node_target}" FORGE_CONTRACT_BUILD_SOURCE_ROOT)
      get_target_property(_base_logical "${_node_target}" FORGE_CONTRACT_MODULE_BASE_DIRS)
      get_target_property(_module_logical "${_node_target}" FORGE_CONTRACT_MODULE_SOURCES)
      get_target_property(_source_logical "${_node_target}" FORGE_CONTRACT_SOURCES)
      get_target_property(_public_header_logical "${_node_target}" FORGE_CONTRACT_PUBLIC_HEADERS)
      get_target_property(_private_header_logical "${_node_target}" FORGE_CONTRACT_PRIVATE_HEADERS)
      set(_bases)
      foreach(_path IN LISTS _base_logical)
         list(APPEND _bases "${_source_root}/${_path}")
      endforeach()
      set(_roots ${_bases})
      if(_source_logical OR _private_header_logical)
         list(APPEND _roots "${_source_root}")
      endif()
      list(REMOVE_DUPLICATES _roots)

      set(_files "[]")
      set(_file_index 0)
      foreach(_role module implementation public_header private_header)
         if(_role STREQUAL "module")
            set(_logical_values ${_module_logical})
         elseif(_role STREQUAL "implementation")
            set(_logical_values ${_source_logical})
         elseif(_role STREQUAL "public_header")
            set(_logical_values ${_public_header_logical})
         else()
            set(_logical_values ${_private_header_logical})
         endif()
         foreach(_logical IN LISTS _logical_values)
            set(_physical "${_source_root}/${_logical}")
            if(NOT EXISTS "${_physical}")
               message(FATAL_ERROR "contract descriptor input does not exist: ${_physical}")
            endif()
            _forge_contract_file_json("${_role}" "${_logical}" "${_physical}" _file)
            string(JSON _files SET "${_files}" ${_file_index} "${_file}")
            math(EXPR _file_index "${_file_index} + 1")
            list(APPEND _source_inputs "${_physical}")
         endforeach()
      endforeach()

      set(_edges "[]")
      set(_edge_index 0)
      foreach(_scope PUBLIC PRIVATE)
         foreach(_kind LIBRARY COMPONENT)
            get_target_property(_ids "${_node_target}" "FORGE_CONTRACT_${_scope}_${_kind}_IDS")
            if(_ids STREQUAL "_ids-NOTFOUND")
               set(_ids)
            endif()
            string(TOLOWER "${_kind}" _kind_value)
            foreach(_dependency_id IN LISTS _ids)
               _forge_contract_edge_json("${_kind_value}" "${_dependency_id}" "${_scope}" _edge)
               string(JSON _edges SET "${_edges}" ${_edge_index} "${_edge}")
               math(EXPR _edge_index "${_edge_index} + 1")
            endforeach()
         endforeach()
      endforeach()

      _forge_contract_json_quote("${_id}" _quoted_id)
      _forge_contract_json_array("${_roots}" _roots_json)
      _forge_contract_json_array("${_bases}" _bases_json)
      set(_node "{\"id\":${_quoted_id},\"source_roots\":${_roots_json},\"module_bases\":${_bases_json},\"files\":${_files},\"dependencies\":${_edges}}")
      string(JSON _nodes SET "${_nodes}" ${_node_index} "${_node}")
      math(EXPR _node_index "${_node_index} + 1")
      list(APPEND _library_targets "${_node_target}")
      list(APPEND _module_bases ${_bases})
   endforeach()

   _forge_contract_json_array("${_root_library_ids}" _root_libraries)
   _forge_contract_json_array("${_root_component_ids}" _root_components)
   set(_root_files "[]")
   set(_root_file_index 0)
   foreach(_role source header compile_check)
      if(_role STREQUAL "source")
         set(_logical_values ${ARG_SOURCE_LOGICAL})
         set(_physical_values ${ARG_SOURCES})
      elseif(_role STREQUAL "header")
         set(_logical_values ${ARG_HEADER_LOGICAL})
         set(_physical_values ${ARG_HEADERS})
      else()
         set(_logical_values ${ARG_COMPILE_CHECK_LOGICAL})
         set(_physical_values ${ARG_COMPILE_CHECKS})
      endif()
      list(LENGTH _logical_values _count)
      if(_count GREATER 0)
         math(EXPR _last "${_count} - 1")
         foreach(_index RANGE 0 ${_last})
            list(GET _logical_values ${_index} _logical)
            list(GET _physical_values ${_index} _physical)
            set(_file_role "${_role}")
            if(_role STREQUAL "source" AND _physical STREQUAL ARG_DISPATCH_SOURCE)
               set(_file_role dispatch_source)
            endif()
            _forge_contract_file_json("${_file_role}" "${_logical}" "${_physical}" _file)
            string(JSON _root_files SET "${_root_files}" ${_root_file_index} "${_file}")
            math(EXPR _root_file_index "${_root_file_index} + 1")
         endforeach()
      endif()
   endforeach()
   foreach(_ricardian CONTRACTS CLAUSES)
      if(ARG_RICARDIAN_${_ricardian})
         string(TOLOWER "${_ricardian}" _suffix)
         _forge_contract_file_json(
            "ricardian_${_suffix}"
            "contract/ricardian/${_suffix}/${ARG_RICARDIAN_${_ricardian}_LOGICAL}"
            "${ARG_RICARDIAN_${_ricardian}}"
            _file
         )
         string(JSON _root_files SET "${_root_files}" ${_root_file_index} "${_file}")
         math(EXPR _root_file_index "${_root_file_index} + 1")
      endif()
   endforeach()
   _forge_contract_json_quote("contract:${ARG_CONTRACT}" _root_owner)
   _forge_contract_json_quote("${ARG_SOURCE_ROOT}" _root_source_root)
   set(_json "{\"schema\":2,\"root\":{\"owner\":${_root_owner},\"source_root\":${_root_source_root},\"files\":${_root_files},\"libraries\":${_root_libraries},\"components\":${_root_components}},\"libraries\":${_nodes},\"components\":${_components}}")
   set(_path "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.contract-graph.json")
   file(WRITE "${_path}" "${_json}\n")
   file(SHA256 "${_path}" _hash)

   list(REMOVE_DUPLICATES _component_targets)
   list(REMOVE_DUPLICATES _library_targets)
   list(REMOVE_DUPLICATES _module_bases)
   list(REMOVE_DUPLICATES _source_inputs)
   set(${ARG_OUTPUT_FILE} "${_path}" PARENT_SCOPE)
   set(${ARG_OUTPUT_HASH} "${_hash}" PARENT_SCOPE)
   set(${ARG_BUILD_DEPENDENCIES} "${_root_targets}" PARENT_SCOPE)
   if(ARG_LIBRARY_TARGETS)
      set(${ARG_LIBRARY_TARGETS} "${_library_targets}" PARENT_SCOPE)
   endif()
   if(ARG_COMPONENT_TARGETS)
      set(${ARG_COMPONENT_TARGETS} "${_component_targets}" PARENT_SCOPE)
   endif()
   if(ARG_MODULE_BASES)
      set(${ARG_MODULE_BASES} "${_module_bases}" PARENT_SCOPE)
   endif()
   if(ARG_SOURCE_INPUTS)
      set(${ARG_SOURCE_INPUTS} "${_source_inputs}" PARENT_SCOPE)
   endif()
endfunction()

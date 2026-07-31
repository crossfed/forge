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

function(forge_add_contract target)
   if(NOT FORGE_CONTRACT_GUEST)
      message(
         FATAL_ERROR
         "forge_add_contract(${target}) is valid only in a project configured "
         "with ForgeContractToolchain; use forge_add_contract_project from a host build"
      )
   endif()
   cmake_parse_arguments(
      ARG
      ""
      "CONTRACT;SOURCE_ROOT;DISPATCH_SOURCE;RICARDIAN_CONTRACTS;RICARDIAN_CLAUSES"
      "SOURCES;COMPILE_CHECKS;LIBRARIES"
      ${ARGN}
   )
   if(ARG_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "forge_add_contract(${target}) received unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
   endif()
   if(NOT ARG_SOURCES)
      message(FATAL_ERROR "forge_add_contract(${target}) requires SOURCES")
   endif()
   if(TARGET "${target}")
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

   set(_all_inputs)
   set(_sources)
   foreach(_source IN LISTS ARG_SOURCES)
      _forge_contract_normalize_file(
         "${_source_root}" "${_source}" "contract source" _absolute
      )
      if(_absolute IN_LIST _all_inputs)
         message(FATAL_ERROR "contract input is declared more than once: ${_absolute}")
      endif()
      list(APPEND _all_inputs "${_absolute}")
      list(APPEND _sources "${_absolute}")
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
         _dispatch_source
      )
      list(FIND _sources "${_dispatch_source}" _dispatch_source_index)
      if(_dispatch_source_index EQUAL -1)
         message(FATAL_ERROR "forge_add_contract(${target}) DISPATCH_SOURCE must also be listed in SOURCES")
      endif()
   endif()

   set(_compile_checks)
   foreach(_source IN LISTS ARG_COMPILE_CHECKS)
      _forge_contract_normalize_file(
         "${_source_root}" "${_source}" "contract compile-check source" _absolute
      )
      if(_absolute IN_LIST _all_inputs)
         message(FATAL_ERROR "contract input is declared more than once: ${_absolute}")
      endif()
      list(APPEND _all_inputs "${_absolute}")
      list(APPEND _compile_checks "${_absolute}")
   endforeach()

   set(_ricardian_contracts)
   if(ARG_RICARDIAN_CONTRACTS)
      _forge_contract_normalize_file(
         "${_source_root}" "${ARG_RICARDIAN_CONTRACTS}" "Ricardian contracts file"
         _ricardian_contracts
      )
   endif()
   set(_ricardian_clauses)
   if(ARG_RICARDIAN_CLAUSES)
      _forge_contract_normalize_file(
         "${_source_root}" "${ARG_RICARDIAN_CLAUSES}" "Ricardian clauses file"
         _ricardian_clauses
      )
   endif()

   string(SHA256 _dependency_key "${CMAKE_CURRENT_BINARY_DIR}/${target}")
   set(_dependency_key "FORGE_CONTRACT_BUILD_${_dependency_key}")
   _forge_contract_collect_dependencies(
      KEY "${_dependency_key}"
      LIBRARIES ${ARG_LIBRARIES}
      OWNER_IDS _owner_ids
      TARGETS _owner_targets
      MODULE_BASES _module_bases
   )

   _forge_contract_owner_target("forge.contract.runtime" _runtime_target)
   get_property(
      _eosio_target GLOBAL PROPERTY FORGE_CONTRACT_GUEST_EOSIO_TARGET
   )
   if(NOT _eosio_target)
      message(FATAL_ERROR "Forge Contract SDK guest compatibility target is unavailable")
   endif()

   if(FORGE_CONTRACT_ARTIFACT_DIR)
      get_filename_component(
         _output_dir "${FORGE_CONTRACT_ARTIFACT_DIR}" ABSOLUTE
         BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
      )
   else()
      set(_output_dir "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
   endif()
   file(MAKE_DIRECTORY "${_output_dir}")
   set(_generated "${CMAKE_CURRENT_BINARY_DIR}/${target}.generated")
   set(_abi "${_output_dir}/${target}.abi")
   set(_dispatcher "${_generated}/${target}.dispatcher.cpp")
   set(_manifest "${_output_dir}/${target}.contract.json")
   set(_depfile "${_generated}/${target}.abi.d")

   set(_implementation_sources ${_sources})
   list(REMOVE_ITEM _implementation_sources "${_dispatch_source}")
   set(_implementation_wrappers)
   set(_wrapper_index 1)
   foreach(_source IN LISTS _implementation_sources)
      list(
         APPEND _implementation_wrappers
         "${_generated}/${target}.source-${_wrapper_index}.cpp"
      )
      math(EXPR _wrapper_index "${_wrapper_index} + 1")
   endforeach()

   set(_module_configuration_directory)
   if(CMAKE_CONFIGURATION_TYPES)
      set(_module_configuration_directory "$<CONFIG>/")
   endif()
   set(_module_search_paths)
   foreach(_module_target IN LISTS _owner_targets)
      get_target_property(_module_binary_dir "${_module_target}" BINARY_DIR)
      if(NOT _module_binary_dir)
         message(FATAL_ERROR "cannot determine module build directory for ${_module_target}")
      endif()
      list(
         APPEND _module_search_paths
         "${_module_binary_dir}/CMakeFiles/${_module_target}.dir/${_module_configuration_directory}"
      )
   endforeach()

   file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contract-compilations")
   set(_contract_compilation_arguments)
   set(_contract_compilation_object_lists)
   foreach(_library_target IN LISTS _owner_targets)
      get_target_property(
         _owner "${_library_target}" FORGE_CONTRACT_OWNER_ID
      )
      if(NOT _owner)
         message(FATAL_ERROR "contract library has no owner: ${_library_target}")
      endif()
      _forge_contract_id_key("${_owner}" _owner_key)
      _forge_contract_object_list(
         "${_library_target}" "library-${_owner_key}" _object_list
      )
      list(
         APPEND _contract_compilation_arguments
         --library-compilation "${_owner}" "${_object_list}"
      )
      list(APPEND _contract_compilation_object_lists "${_object_list}")
      foreach(_scope PUBLIC PRIVATE)
         get_target_property(
            _dependencies
            "${_library_target}"
            "FORGE_CONTRACT_${_scope}_OWNER_IDS"
         )
         if(_dependencies STREQUAL "_dependencies-NOTFOUND")
            set(_dependencies)
         endif()
         string(TOLOWER "${_scope}" _scope_name)
         foreach(_dependency IN LISTS _dependencies)
            list(
               APPEND _contract_compilation_arguments
               --library-dependency "${_owner}" "${_scope_name}" "${_dependency}"
            )
         endforeach()
      endforeach()
   endforeach()

   get_property(
      _root_owner_ids GLOBAL PROPERTY FORGE_CONTRACT_FOUNDATION_COMPONENT_IDS
   )
   foreach(_dependency IN LISTS ARG_LIBRARIES)
      _forge_contract_dependency(
         "${_dependency}" "root" _unused_target _root_owner
      )
      list(APPEND _root_owner_ids "${_root_owner}")
   endforeach()
   list(REMOVE_DUPLICATES _root_owner_ids)
   foreach(_root_owner IN LISTS _root_owner_ids)
      list(APPEND _contract_compilation_arguments --root-library "${_root_owner}")
   endforeach()
   get_property(
      _known_component_ids GLOBAL PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS
   )
   foreach(_component_id IN LISTS _known_component_ids)
      _forge_contract_id_key("${_component_id}" _component_key)
      get_property(
         _known_modules GLOBAL
         PROPERTY "FORGE_CONTRACT_COMPONENT_${_component_key}_MODULE_NAMES"
      )
      foreach(_known_module IN LISTS _known_modules)
         list(
            APPEND _contract_compilation_arguments
            --known-module "${_known_module}" "${_component_id}"
         )
      endforeach()
   endforeach()

   set(
      _abigen_command
      "${ForgeContract_ABIGEN}"
      --contract "${ARG_CONTRACT}"
      --abi "${_abi}"
      --dispatch "${_dispatcher}"
      --depfile "${_depfile}"
      --attribute-plugin "${ForgeContract_ATTR_PLUGIN}"
      --sysroot "${CMAKE_SYSROOT}"
      --sdk-include "${ForgeContract_DATA_DIR}/include"
      --sdk-include "${ForgeContract_DATA_DIR}/modules"
      --include "${_source_root}"
   )
   list(APPEND _abigen_command ${_contract_compilation_arguments})
   foreach(_source IN LISTS _compile_checks)
      list(APPEND _abigen_command --dependency-source "${_source}")
   endforeach()
   foreach(_include_directory IN LISTS _module_bases)
      list(APPEND _abigen_command --include "${_include_directory}")
   endforeach()
   foreach(_module_path IN LISTS _module_search_paths)
      list(APPEND _abigen_command --module-path "${_module_path}")
   endforeach()
   foreach(_wrapper IN LISTS _implementation_wrappers)
      list(APPEND _abigen_command --source-wrapper "${_wrapper}")
   endforeach()
   if(_ricardian_contracts)
      list(APPEND _abigen_command --ricardian-contracts "${_ricardian_contracts}")
   endif()
   if(_ricardian_clauses)
      list(APPEND _abigen_command --ricardian-clauses "${_ricardian_clauses}")
   endif()
   list(APPEND _abigen_command ${_sources})

   add_custom_command(
      OUTPUT "${_abi}" "${_dispatcher}" ${_implementation_wrappers}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_generated}"
      COMMAND ${_abigen_command}
      DEPENDS
         "${_runtime_target}"
         "${ForgeContract_ABIGEN}"
         "${ForgeContract_ATTR_PLUGIN}"
         ${_sources}
         ${_compile_checks}
         ${_ricardian_contracts}
         ${_ricardian_clauses}
         ${_owner_targets}
         ${_contract_compilation_object_lists}
      DEPFILE "${_depfile}"
      COMMAND_EXPAND_LISTS
      VERBATIM
   )

   add_executable("${target}" "${_dispatcher}" ${_implementation_wrappers})
   set_source_files_properties(
      "${_dispatcher}" ${_implementation_wrappers}
      PROPERTIES OBJECT_DEPENDS "${ForgeContract_ATTR_PLUGIN}"
   )
   target_link_libraries(
      "${target}"
      PRIVATE
         "${_runtime_target}"
         "${_eosio_target}"
         ${ARG_LIBRARIES}
   )
   target_include_directories(
      "${target}"
      PRIVATE
         "${ForgeContract_DATA_DIR}/include"
         "${_source_root}"
         ${_module_bases}
   )
   _forge_contract_configure_guest_target("${target}")
   target_compile_options(
      "${target}"
      PRIVATE
         -Werror=return-type
         "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
         "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=."
         "-ffile-prefix-map=${_source_root}=./source"
   )
   foreach(_module_path IN LISTS _module_search_paths)
      target_compile_options(
         "${target}" PRIVATE "-fprebuilt-module-path=${_module_path}"
      )
   endforeach()

   if(_compile_checks)
      add_library("${target}_compile_checks" OBJECT ${_compile_checks})
      set_source_files_properties(
         ${_compile_checks}
         PROPERTIES OBJECT_DEPENDS "${ForgeContract_ATTR_PLUGIN}"
      )
      target_link_libraries(
         "${target}_compile_checks"
         PRIVATE
            "${_runtime_target}"
            "${_eosio_target}"
            ${ARG_LIBRARIES}
      )
      target_include_directories(
         "${target}_compile_checks"
         PRIVATE
            "${ForgeContract_DATA_DIR}/include"
            "${_source_root}"
            ${_module_bases}
      )
      _forge_contract_configure_guest_target("${target}_compile_checks")
      foreach(_module_path IN LISTS _module_search_paths)
         target_compile_options(
            "${target}_compile_checks"
            PRIVATE "-fprebuilt-module-path=${_module_path}"
         )
      endforeach()
      add_dependencies("${target}" "${target}_compile_checks")
   endif()

   target_link_options(
      "${target}"
      PRIVATE
         -nostdlib
         "-Wl,--no-entry"
         "-Wl,--export=apply"
         "-Wl,--export-if-defined=__forge_call"
         "-Wl,--export-memory"
         "-Wl,--stack-first"
         "-Wl,-z,stack-size=8192"
         "-Wl,--initial-memory=131072"
         "-Wl,--max-memory=16777216"
         "-Wl,--allow-undefined"
         "-Wl,--gc-sections"
         "-Wl,--strip-all"
         "-Wl,--no-merge-data-segments"
   )

   find_library(
      _libcxx NAMES c++
      PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32"
      NO_DEFAULT_PATH
   )
   find_library(
      _libcxxabi NAMES c++abi
      PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32"
      NO_DEFAULT_PATH
   )
   find_library(
      _libc NAMES c
      PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32"
      NO_DEFAULT_PATH
   )
   find_library(
      _libm NAMES m
      PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32"
      NO_DEFAULT_PATH
   )
   find_library(
      _builtins
      NAMES clang_rt.builtins-wasm32
      PATHS
         "${CMAKE_SYSROOT}/lib"
         "${CMAKE_SYSROOT}/lib/wasm32"
         "${CMAKE_SYSROOT}/lib/generic"
      NO_DEFAULT_PATH
   )
   find_library(
      _runtime NAMES forge_guest_runtime
      PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32"
      NO_DEFAULT_PATH
   )
   if(NOT _libcxx)
      message(FATAL_ERROR "Forge Contract sysroot is missing libc++")
   endif()
   if(NOT _runtime)
      message(FATAL_ERROR "Forge Contract sysroot is missing the guest runtime")
   endif()
   if(ForgeContract_PROFILE STREQUAL "release" AND (NOT _libcxxabi OR NOT _builtins))
      message(FATAL_ERROR "release Forge Contract sysroot requires libc++abi and compiler-rt builtins")
   endif()
   set(_guest_libraries "${_libcxx}")
   foreach(_library _libcxxabi _libc _libm _builtins)
      if(${_library})
         list(APPEND _guest_libraries "${${_library}}")
      endif()
   endforeach()
   list(APPEND _guest_libraries "${_runtime}")
   target_link_libraries("${target}" PRIVATE ${_guest_libraries})

   set_target_properties(
      "${target}"
      PROPERTIES
         OUTPUT_NAME "${target}"
         SUFFIX ".wasm"
         RUNTIME_OUTPUT_DIRECTORY "${_output_dir}"
         FORGE_CONTRACT_WASM_FILE "${_output_dir}/${target}.wasm"
         FORGE_CONTRACT_ABI_FILE "${_abi}"
         FORGE_CONTRACT_MANIFEST_FILE "${_manifest}"
   )
   foreach(_configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
      string(TOUPPER "${_configuration}" _configuration_upper)
      set_target_properties(
         "${target}"
         PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${_configuration_upper}" "${_output_dir}"
      )
   endforeach()

   set(_manifest_llvm_commit_args)
   if(NOT ForgeContract_LLVM_COMMIT STREQUAL "")
      list(APPEND _manifest_llvm_commit_args --llvm-commit "${ForgeContract_LLVM_COMMIT}")
   endif()
   set(_intrinsics "${ForgeContract_DATA_DIR}/registry/intrinsics.json")
   add_custom_command(
      OUTPUT "${_manifest}"
      COMMAND
         "${ForgeContract_CHECK}"
         --wasm "$<TARGET_FILE:${target}>"
         --abi "${_abi}"
         --imports "${_intrinsics}"
         --required-export apply
      COMMAND
         "${ForgeContract_MANIFEST}"
         --wasm "$<TARGET_FILE:${target}>"
         --abi "${_abi}"
         --imports "${_intrinsics}"
         --output "${_manifest}"
         --sdk-version "${ForgeContract_VERSION}"
         --llvm-version "${ForgeContract_LLVM_VERSION}"
         ${_manifest_llvm_commit_args}
         --sysroot-version "${ForgeContract_SYSROOT_SCHEMA_VERSION}"
         --sysroot-hash "${ForgeContract_SYSROOT_HASH}"
         --intrinsic-version "${ForgeContract_INTRINSIC_VERSION}"
         --profile "${ForgeContract_PROFILE}"
         --reproducible "${ForgeContract_REPRODUCIBLE}"
      DEPENDS
         "$<TARGET_FILE:${target}>"
         "${_abi}"
         "${_intrinsics}"
         "${ForgeContract_CHECK}"
         "${ForgeContract_MANIFEST}"
      VERBATIM
   )
   add_custom_target("${target}_artifacts" ALL DEPENDS "${_manifest}")
   set_target_properties(
      "${target}_artifacts"
      PROPERTIES
         FORGE_CONTRACT_WASM_FILE "${_output_dir}/${target}.wasm"
         FORGE_CONTRACT_ABI_FILE "${_abi}"
         FORGE_CONTRACT_MANIFEST_FILE "${_manifest}"
   )
endfunction()

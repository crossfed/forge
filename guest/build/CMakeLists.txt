cmake_minimum_required(VERSION 3.31)

project(ForgeContractConsumer LANGUAGES CXX)

foreach(_required
   FORGE_CONTRACT_TARGET
   FORGE_CONTRACT_GRAPH_FILE
   FORGE_CONTRACT_GRAPH_HASH
   FORGE_CONTRACT_COMPONENTS
   FORGE_CONTRACT_OUTPUT_DIR
   FORGE_CONTRACT_DATA_DIR
   FORGE_CONTRACT_ABIGEN
   FORGE_CONTRACT_ATTR_PLUGIN
   FORGE_CONTRACT_CHECK
   FORGE_CONTRACT_MANIFEST
)
   if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
      message(FATAL_ERROR "missing required Forge Contract build setting: ${_required}")
   endif()
endforeach()

if(NOT EXISTS "${FORGE_CONTRACT_GRAPH_FILE}")
   message(FATAL_ERROR "Forge Contract graph does not exist: ${FORGE_CONTRACT_GRAPH_FILE}")
endif()
file(SHA256 "${FORGE_CONTRACT_GRAPH_FILE}" _contract_graph_hash)
if(NOT _contract_graph_hash STREQUAL FORGE_CONTRACT_GRAPH_HASH)
   message(FATAL_ERROR "Forge Contract graph hash mismatch")
endif()
file(READ "${FORGE_CONTRACT_GRAPH_FILE}" _contract_graph)
string(JSON _contract_graph_schema GET "${_contract_graph}" schema)
if(NOT _contract_graph_schema EQUAL 2)
   message(FATAL_ERROR "unsupported Forge Contract graph schema: ${_contract_graph_schema}")
endif()

string(JSON _root_owner GET "${_contract_graph}" root owner)
if(NOT _root_owner MATCHES "^contract:(.+)$")
   message(FATAL_ERROR "Forge Contract graph has an invalid root owner: ${_root_owner}")
endif()
set(FORGE_CONTRACT_NAME "${CMAKE_MATCH_1}")
string(JSON FORGE_CONTRACT_SOURCE_ROOT GET "${_contract_graph}" root source_root)
if(NOT IS_DIRECTORY "${FORGE_CONTRACT_SOURCE_ROOT}")
   message(FATAL_ERROR "Forge Contract graph root does not exist: ${FORGE_CONTRACT_SOURCE_ROOT}")
endif()

set(_dispatch_source)
set(_other_sources)
set(FORGE_CONTRACT_HEADERS)
set(FORGE_CONTRACT_COMPILE_CHECKS)
set(FORGE_CONTRACT_RICARDIAN_CONTRACTS)
set(FORGE_CONTRACT_RICARDIAN_CLAUSES)
string(JSON _root_file_count LENGTH "${_contract_graph}" root files)
if(_root_file_count GREATER 0)
   math(EXPR _root_file_last "${_root_file_count} - 1")
   foreach(_index RANGE 0 ${_root_file_last})
      string(JSON _role GET "${_contract_graph}" root files ${_index} role)
      string(JSON _path GET "${_contract_graph}" root files ${_index} physical_path)
      if(NOT EXISTS "${_path}" OR IS_DIRECTORY "${_path}")
         message(FATAL_ERROR "Forge Contract graph root input is not a file: ${_path}")
      endif()
      if(_role STREQUAL "dispatch_source")
         if(_dispatch_source)
            message(FATAL_ERROR "Forge Contract graph has multiple dispatch sources")
         endif()
         set(_dispatch_source "${_path}")
      elseif(_role STREQUAL "source")
         list(APPEND _other_sources "${_path}")
      elseif(_role STREQUAL "header")
         list(APPEND FORGE_CONTRACT_HEADERS "${_path}")
      elseif(_role STREQUAL "compile_check")
         list(APPEND FORGE_CONTRACT_COMPILE_CHECKS "${_path}")
      elseif(_role STREQUAL "ricardian_contracts")
         set(FORGE_CONTRACT_RICARDIAN_CONTRACTS "${_path}")
      elseif(_role STREQUAL "ricardian_clauses")
         set(FORGE_CONTRACT_RICARDIAN_CLAUSES "${_path}")
      else()
         message(FATAL_ERROR "Forge Contract graph has an invalid root file role: ${_role}")
      endif()
   endforeach()
endif()
if(NOT _dispatch_source)
   message(FATAL_ERROR "Forge Contract graph has no dispatch source")
endif()
set(FORGE_CONTRACT_SOURCES "${_dispatch_source}" ${_other_sources})
set(_implementation_sources ${FORGE_CONTRACT_SOURCES})
list(REMOVE_AT _implementation_sources 0)

set(_modules "${FORGE_CONTRACT_DATA_DIR}/modules")
set(_include "${FORGE_CONTRACT_DATA_DIR}/include")
set(_runtime_archive "${CMAKE_SYSROOT}/lib/libforge_guest_runtime.a")
set(_contract_archive "${CMAKE_SYSROOT}/lib/libforge_guest_contract.a")
set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_abi "${FORGE_CONTRACT_OUTPUT_DIR}/${FORGE_CONTRACT_TARGET}.abi")
set(_dispatcher "${_generated}/${FORGE_CONTRACT_TARGET}.dispatcher.cpp")
set(_implementation_wrappers)
set(_wrapper_index 1)
foreach(_source IN LISTS _implementation_sources)
   list(APPEND _implementation_wrappers "${_generated}/${FORGE_CONTRACT_TARGET}.source-${_wrapper_index}.cpp")
   math(EXPR _wrapper_index "${_wrapper_index} + 1")
endforeach()
set(_depfile "${_generated}/${FORGE_CONTRACT_TARGET}.abi.d")
set(_manifest "${FORGE_CONTRACT_OUTPUT_DIR}/${FORGE_CONTRACT_TARGET}.contract.json")
# Deployable WASM must remain within chain limits in every host configuration.
# Debug builds retain symbols while using the same optimization as CDT contracts.
set(_contract_optimization -O3)

function(_forge_contract_component_key id output)
   string(SHA256 _key "${id}")
   string(SUBSTRING "${_key}" 0 16 _key)
   set(${output} "${_key}" PARENT_SCOPE)
endfunction()

function(forge_contract_register_guest_component)
   cmake_parse_arguments(
      ARG
      ""
      "ID;ARCHIVE"
      "MODULES;MODULE_NAMES;PUBLIC_LIBRARIES"
      ${ARGN}
   )
   if(
      ARG_UNPARSED_ARGUMENTS
      OR NOT ARG_ID
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
   _forge_contract_component_key("${ARG_ID}" _key)
   get_property(_existing GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_ID")
   if(_existing)
      message(FATAL_ERROR "duplicate Forge Contract guest component ID: ${ARG_ID}")
   endif()
   set(_target "forge_contract_component_${_key}")
   set(_module_paths)
   foreach(_module IN LISTS ARG_MODULES)
      set(_path "${_modules}/${_module}")
      if(NOT EXISTS "${_path}")
         message(FATAL_ERROR "Forge Contract guest component module is missing: ${_path}")
      endif()
      list(APPEND _module_paths "${_path}")
   endforeach()
   add_library("${_target}" STATIC)
   target_sources(
      "${_target}"
      PUBLIC
         FILE_SET forge_contract_component_modules TYPE CXX_MODULES
         BASE_DIRS "${_modules}"
         FILES ${_module_paths}
   )
   if(ARG_ARCHIVE)
      set(_archive "${CMAKE_SYSROOT}/lib/${ARG_ARCHIVE}")
      if(NOT EXISTS "${_archive}")
         message(FATAL_ERROR "Forge Contract guest component archive is missing: ${_archive}")
      endif()
      add_library("${_target}_archive" STATIC IMPORTED GLOBAL)
      set_target_properties("${_target}_archive" PROPERTIES IMPORTED_LOCATION "${_archive}")
      target_link_libraries("${_target}" PUBLIC "${_target}_archive")
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_ID" "${ARG_ID}")
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_TARGET" "${_target}")
   set_property(
      GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_MODULE_NAMES"
      "${ARG_MODULE_NAMES}"
   )
   set_property(
      GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_DEPENDENCIES"
      "${ARG_PUBLIC_LIBRARIES}"
   )
   set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS "${ARG_ID}")
endfunction()

function(_forge_contract_guest_component_target id output)
   _forge_contract_component_key("${id}" _key)
   get_property(_registered GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_ID")
   get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_TARGET")
   if(NOT _registered STREQUAL id OR NOT _target)
      message(FATAL_ERROR "unknown Forge Contract guest component ID: ${id}")
   endif()
   set(${output} "${_target}" PARENT_SCOPE)
endfunction()

include("${FORGE_CONTRACT_COMPONENTS}")
get_property(_guest_component_ids GLOBAL PROPERTY FORGE_CONTRACT_GUEST_COMPONENT_IDS)
foreach(_id IN LISTS _guest_component_ids)
   _forge_contract_component_key("${_id}" _key)
   get_property(_target GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_TARGET")
   get_property(
      _dependencies GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_DEPENDENCIES"
   )
   foreach(_dependency IN LISTS _dependencies)
      _forge_contract_guest_component_target("${_dependency}" _dependency_target)
      target_link_libraries("${_target}" PUBLIC "${_dependency_target}")
   endforeach()
endforeach()

string(JSON _descriptor_component_count LENGTH "${_contract_graph}" components)
if(_descriptor_component_count GREATER 0)
   math(EXPR _descriptor_component_last "${_descriptor_component_count} - 1")
   foreach(_index RANGE 0 ${_descriptor_component_last})
      string(JSON _id GET "${_contract_graph}" components ${_index} id)
      _forge_contract_component_key("${_id}" _key)
      get_property(
         _expected_modules GLOBAL PROPERTY
         "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_MODULE_NAMES"
      )
      get_property(
         _expected_dependencies GLOBAL PROPERTY
         "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_DEPENDENCIES"
      )
      if(NOT _expected_modules)
         message(FATAL_ERROR "contract graph references unknown Forge guest component: ${_id}")
      endif()
      set(_actual_modules)
      string(
         JSON _module_count LENGTH
         "${_contract_graph}" components ${_index} modules
      )
      if(_module_count GREATER 0)
         math(EXPR _module_last "${_module_count} - 1")
         foreach(_module_index RANGE 0 ${_module_last})
            string(
               JSON _module GET
               "${_contract_graph}" components ${_index} modules ${_module_index}
            )
            list(APPEND _actual_modules "${_module}")
         endforeach()
      endif()
      set(_actual_dependencies)
      string(
         JSON _dependency_count LENGTH
         "${_contract_graph}" components ${_index} dependencies
      )
      if(_dependency_count GREATER 0)
         math(EXPR _dependency_last "${_dependency_count} - 1")
         foreach(_dependency_index RANGE 0 ${_dependency_last})
            string(
               JSON _dependency GET
               "${_contract_graph}" components ${_index} dependencies
               ${_dependency_index}
            )
            list(APPEND _actual_dependencies "${_dependency}")
         endforeach()
      endif()
      if(
         NOT "${_actual_modules}" STREQUAL "${_expected_modules}"
         OR NOT "${_actual_dependencies}" STREQUAL "${_expected_dependencies}"
      )
         message(FATAL_ERROR "contract graph component descriptor does not match SDK: ${_id}")
      endif()
   endforeach()
endif()

_forge_contract_guest_component_target(forge.raw _raw_target)
_forge_contract_guest_component_target(forge.codec.base64 _codec_base64_target)
_forge_contract_guest_component_target(forge.codec.base58 _codec_base58_target)
_forge_contract_guest_component_target(forge.codec.hex _codec_hex_target)
_forge_contract_guest_component_target(forge.chain.protocol _chain_protocol_target)

foreach(_archive IN ITEMS "${_runtime_archive}" "${_contract_archive}")
   if(NOT EXISTS "${_archive}")
      message(FATAL_ERROR "Forge Contract sysroot is missing ${_archive}")
   endif()
endforeach()
add_library(forge_guest_runtime STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_runtime PROPERTIES IMPORTED_LOCATION "${_runtime_archive}")

add_library(forge_guest_contract STATIC)
target_sources(
   forge_guest_contract
   PUBLIC
      FILE_SET forge_guest_contract_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/contract/intrinsics.cppm"
         "${_modules}/forge/contract/contract.cppm"
         "${_modules}/forge/contract/datastream.cppm"
         "${_modules}/forge/contract/varint.cppm"
         "${_modules}/forge/contract/fixed_bytes.cppm"
         "${_modules}/forge/contract/binary_extension.cppm"
         "${_modules}/forge/contract/ignore.cppm"
         "${_modules}/forge/contract/hash_id.cppm"
         "${_modules}/forge/contract/action.cppm"
         "${_modules}/forge/contract/transaction.cppm"
         "${_modules}/forge/contract/system.cppm"
         "${_modules}/forge/contract/deferred_transaction.cppm"
         "${_modules}/forge/contract/authorization.cppm"
         "${_modules}/forge/contract/bitset.cppm"
         "${_modules}/forge/contract/call.cppm"
         "${_modules}/forge/contract/crypto.cppm"
         "${_modules}/forge/contract/crypto_bls_ext.cppm"
         "${_modules}/forge/contract/crypto_ext.cppm"
         "${_modules}/forge/contract/instant_finality.cppm"
         "${_modules}/forge/contract/key.cppm"
         "${_modules}/forge/contract/powers.cppm"
         "${_modules}/forge/contract/print.cppm"
         "${_modules}/forge/contract/privileged.cppm"
         "${_modules}/forge/contract/producer_schedule.cppm"
         "${_modules}/forge/contract/rope.cppm"
         "${_modules}/forge/contract/string.cppm"
         "${_modules}/forge/contract/dispatcher.cppm"
         "${_modules}/forge/contract/multi_index.cppm"
         "${_modules}/forge/contract/singleton.cppm"
         "${_modules}/forge/contract/compatibility_name.cppm"
         "${_modules}/forge/contract/compatibility_asset.cppm"
)
add_library(forge_guest_contract_implementation STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_contract_implementation PROPERTIES IMPORTED_LOCATION "${_contract_archive}")
target_link_libraries(
   forge_guest_contract
   PUBLIC
      forge_guest_contract_implementation
      ${_raw_target}
      ${_chain_protocol_target}
      ${_codec_base64_target}
      ${_codec_base58_target}
      ${_codec_hex_target}
)

add_library(forge_guest_eosio INTERFACE)
target_include_directories(forge_guest_eosio INTERFACE "${_include}")
target_link_libraries(forge_guest_eosio INTERFACE forge_guest_contract)

function(_forge_contract_library_target id output)
   _forge_contract_component_key("${id}" _key)
   if(NOT DEFINED "_forge_contract_library_target_${_key}")
      message(FATAL_ERROR "contract library dependency is missing from graph: ${id}")
   endif()
   set(${output} "${_forge_contract_library_target_${_key}}" PARENT_SCOPE)
endfunction()

function(_forge_contract_collect_visible_component id)
   _forge_contract_component_key("${id}" _key)
   get_property(_visited GLOBAL PROPERTY "FORGE_CONTRACT_VISIBLE_COMPONENT_${_key}")
   if(_visited)
      return()
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_VISIBLE_COMPONENT_${_key}" TRUE)
   get_property(
      _dependencies GLOBAL PROPERTY "FORGE_CONTRACT_GUEST_COMPONENT_${_key}_DEPENDENCIES"
   )
   foreach(_dependency IN LISTS _dependencies)
      _forge_contract_collect_visible_component("${_dependency}")
   endforeach()
   _forge_contract_guest_component_target("${id}" _target)
   set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_VISIBLE_COMPONENT_TARGETS "${_target}")
endfunction()

function(_forge_contract_collect_visible_library id)
   _forge_contract_component_key("${id}" _key)
   get_property(_visited GLOBAL PROPERTY "FORGE_CONTRACT_VISIBLE_LIBRARY_${_key}")
   if(_visited)
      return()
   endif()
   set_property(GLOBAL PROPERTY "FORGE_CONTRACT_VISIBLE_LIBRARY_${_key}" TRUE)
   get_property(
      _library_dependencies GLOBAL PROPERTY
      "FORGE_CONTRACT_LIBRARY_${_key}_PUBLIC_LIBRARY_IDS"
   )
   get_property(
      _component_dependencies GLOBAL PROPERTY
      "FORGE_CONTRACT_LIBRARY_${_key}_PUBLIC_COMPONENT_IDS"
   )
   foreach(_dependency IN LISTS _library_dependencies)
      _forge_contract_collect_visible_library("${_dependency}")
   endforeach()
   foreach(_dependency IN LISTS _component_dependencies)
      _forge_contract_collect_visible_component("${_dependency}")
   endforeach()
   _forge_contract_library_target("${id}" _target)
   get_property(
      _module_bases GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_${_key}_MODULE_BASES"
   )
   set_property(GLOBAL APPEND PROPERTY FORGE_CONTRACT_VISIBLE_LIBRARY_TARGETS "${_target}")
   set_property(
      GLOBAL APPEND PROPERTY FORGE_CONTRACT_VISIBLE_LIBRARY_MODULE_BASES "${_module_bases}"
   )
endfunction()

set(_contract_library_targets)
set(_contract_library_source_paths)
string(JSON _library_count LENGTH "${_contract_graph}" libraries)
if(_library_count GREATER 0)
   math(EXPR _library_last "${_library_count} - 1")
   foreach(_library_index RANGE 0 ${_library_last})
      string(JSON _id GET "${_contract_graph}" libraries ${_library_index} id)
      _forge_contract_component_key("${_id}" _key)
      set(_target "forge_contract_library_${_key}")
      if(DEFINED "_forge_contract_library_target_${_key}")
         message(FATAL_ERROR "duplicate contract library ID in graph: ${_id}")
      endif()
      set("_forge_contract_library_target_${_key}" "${_target}")

      set(_module_bases)
      string(JSON _base_count LENGTH "${_contract_graph}" libraries ${_library_index} module_bases)
      if(_base_count GREATER 0)
         math(EXPR _base_last "${_base_count} - 1")
         foreach(_base_index RANGE 0 ${_base_last})
            string(
               JSON _base GET
               "${_contract_graph}" libraries ${_library_index} module_bases ${_base_index}
            )
            list(APPEND _module_bases "${_base}")
         endforeach()
      endif()

      set(_modules)
      set(_implementation)
      set(_headers)
      string(JSON _file_count LENGTH "${_contract_graph}" libraries ${_library_index} files)
      if(_file_count GREATER 0)
         math(EXPR _file_last "${_file_count} - 1")
         foreach(_file_index RANGE 0 ${_file_last})
            string(
               JSON _role GET
               "${_contract_graph}" libraries ${_library_index} files ${_file_index} role
            )
            string(
               JSON _path GET
               "${_contract_graph}" libraries ${_library_index} files ${_file_index} physical_path
            )
            if(NOT EXISTS "${_path}")
               message(FATAL_ERROR "contract graph input does not exist: ${_path}")
            endif()
            if(_role STREQUAL "module")
               list(APPEND _modules "${_path}")
            elseif(_role STREQUAL "implementation")
               list(APPEND _implementation "${_path}")
            elseif(_role STREQUAL "public_header" OR _role STREQUAL "private_header")
               list(APPEND _headers "${_path}")
            else()
               message(FATAL_ERROR "contract graph has unknown file role: ${_role}")
            endif()
            list(APPEND _contract_library_source_paths "${_path}")
         endforeach()
      endif()
      if(NOT _modules)
         message(FATAL_ERROR "contract library has no module inputs: ${_id}")
      endif()

      set(_public_dependencies)
      set(_private_dependencies)
      set(_public_library_ids)
      set(_public_component_ids)
      string(
         JSON _dependency_count LENGTH
         "${_contract_graph}" libraries ${_library_index} dependencies
      )
      if(_dependency_count GREATER 0)
         math(EXPR _dependency_last "${_dependency_count} - 1")
         foreach(_dependency_index RANGE 0 ${_dependency_last})
            string(
               JSON _kind GET
               "${_contract_graph}" libraries ${_library_index} dependencies
               ${_dependency_index} kind
            )
            string(
               JSON _dependency_id GET
               "${_contract_graph}" libraries ${_library_index} dependencies
               ${_dependency_index} id
            )
            string(
               JSON _scope GET
               "${_contract_graph}" libraries ${_library_index} dependencies
               ${_dependency_index} scope
            )
            if(_kind STREQUAL "component")
               _forge_contract_guest_component_target("${_dependency_id}" _dependency_target)
               if(_scope STREQUAL "public")
                  list(APPEND _public_component_ids "${_dependency_id}")
               endif()
            elseif(_kind STREQUAL "library")
               _forge_contract_library_target("${_dependency_id}" _dependency_target)
               if(_scope STREQUAL "public")
                  list(APPEND _public_library_ids "${_dependency_id}")
               endif()
            else()
               message(FATAL_ERROR "contract graph has unknown dependency kind: ${_kind}")
            endif()
            if(_scope STREQUAL "public")
               list(APPEND _public_dependencies "${_dependency_target}")
            elseif(_scope STREQUAL "private")
               list(APPEND _private_dependencies "${_dependency_target}")
            else()
               message(FATAL_ERROR "contract graph has unknown dependency scope: ${_scope}")
            endif()
         endforeach()
      endif()

      add_library("${_target}" STATIC)
      set_target_properties(
         "${_target}" PROPERTIES FORGE_CONTRACT_LIBRARY_ID "${_id}"
      )
      target_sources(
         "${_target}"
         PUBLIC
            FILE_SET forge_contract_modules TYPE CXX_MODULES
            BASE_DIRS ${_module_bases}
            FILES ${_modules}
      )
      if(_implementation OR _headers)
         target_sources("${_target}" PRIVATE ${_implementation} ${_headers})
      endif()
      set_source_files_properties(
         ${_modules} ${_implementation}
         PROPERTIES OBJECT_DEPENDS "${FORGE_CONTRACT_ATTR_PLUGIN}"
      )
      target_include_directories("${_target}" PUBLIC ${_module_bases})
      if(_public_dependencies)
         target_link_libraries("${_target}" PUBLIC ${_public_dependencies})
      endif()
      if(_private_dependencies)
         target_link_libraries("${_target}" PRIVATE ${_private_dependencies})
      endif()
      list(APPEND _contract_library_targets "${_target}")
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_${_key}_MODULE_BASES" "${_module_bases}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_${_key}_PUBLIC_LIBRARY_IDS"
         "${_public_library_ids}"
      )
      set_property(
         GLOBAL PROPERTY "FORGE_CONTRACT_LIBRARY_${_key}_PUBLIC_COMPONENT_IDS"
         "${_public_component_ids}"
      )
   endforeach()
endif()

set(_root_contract_targets)
set(_root_library_ids)
string(JSON _root_library_count LENGTH "${_contract_graph}" root libraries)
if(_root_library_count GREATER 0)
   math(EXPR _root_library_last "${_root_library_count} - 1")
   foreach(_index RANGE 0 ${_root_library_last})
      string(JSON _id GET "${_contract_graph}" root libraries ${_index})
      _forge_contract_library_target("${_id}" _target)
      list(APPEND _root_contract_targets "${_target}")
      list(APPEND _root_library_ids "${_id}")
   endforeach()
endif()
set(_root_component_ids)
string(JSON _root_component_count LENGTH "${_contract_graph}" root components)
if(_root_component_count GREATER 0)
   math(EXPR _root_component_last "${_root_component_count} - 1")
   foreach(_index RANGE 0 ${_root_component_last})
      string(JSON _id GET "${_contract_graph}" root components ${_index})
      _forge_contract_guest_component_target("${_id}" _target)
      list(APPEND _root_contract_targets "${_target}")
      list(APPEND _root_component_ids "${_id}")
   endforeach()
endif()

set_property(GLOBAL PROPERTY FORGE_CONTRACT_VISIBLE_LIBRARY_TARGETS "")
set_property(GLOBAL PROPERTY FORGE_CONTRACT_VISIBLE_LIBRARY_MODULE_BASES "")
set_property(GLOBAL PROPERTY FORGE_CONTRACT_VISIBLE_COMPONENT_TARGETS "")
foreach(
   _id IN ITEMS
   forge.raw
   forge.codec.base64
   forge.codec.base58
   forge.codec.hex
   forge.chain.protocol
)
   _forge_contract_collect_visible_component("${_id}")
endforeach()
foreach(_id IN LISTS _root_library_ids)
   _forge_contract_collect_visible_library("${_id}")
endforeach()
foreach(_id IN LISTS _root_component_ids)
   _forge_contract_collect_visible_component("${_id}")
endforeach()
get_property(
   _visible_library_targets GLOBAL PROPERTY FORGE_CONTRACT_VISIBLE_LIBRARY_TARGETS
)
get_property(
   _visible_library_module_bases
   GLOBAL PROPERTY FORGE_CONTRACT_VISIBLE_LIBRARY_MODULE_BASES
)
get_property(
   _visible_component_targets GLOBAL PROPERTY FORGE_CONTRACT_VISIBLE_COMPONENT_TARGETS
)
list(REMOVE_DUPLICATES _visible_library_targets)
list(REMOVE_DUPLICATES _visible_library_module_bases)
list(REMOVE_DUPLICATES _visible_component_targets)

foreach(
   _target
   ${_guest_component_ids}
   ${_contract_library_targets}
   forge_guest_contract
)
   if(_target IN_LIST _guest_component_ids)
      _forge_contract_guest_component_target("${_target}" _target)
   endif()
   target_include_directories(${_target} PRIVATE "${_include}")
   target_compile_definitions(${_target} PRIVATE FORGE_CONTRACT_GUEST=1)
   target_compile_options(
      ${_target}
      PRIVATE
      -fplugin=${FORGE_CONTRACT_ATTR_PLUGIN}
      -fno-exceptions
      -fno-rtti
      -fno-threadsafe-statics
      -ffreestanding
      -fvisibility=hidden
      -fno-ident
      -mcpu=mvp
      ${_contract_optimization}
   )
endforeach()

set(_module_configuration_directory "")
if(CMAKE_CONFIGURATION_TYPES)
   set(_module_configuration_directory "$<CONFIG>/")
endif()
set(
   _module_targets
   forge_guest_contract
   ${_visible_library_targets}
   ${_visible_component_targets}
)
list(REMOVE_DUPLICATES _module_targets)
set(_module_search_paths)
foreach(_target IN LISTS _module_targets)
   list(
      APPEND _module_search_paths
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${_target}.dir/${_module_configuration_directory}"
   )
endforeach()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contract-compilations")
function(_forge_contract_generate_object_list target identity output)
   set(
      _path
      "${CMAKE_CURRENT_BINARY_DIR}/contract-compilations/${identity}-$<CONFIG>.objects"
   )
   file(
      GENERATE
      OUTPUT "${_path}"
      CONTENT "$<JOIN:$<TARGET_OBJECTS:${target}>,\n>\n"
   )
   set(${output} "${_path}" PARENT_SCOPE)
endfunction()

set(_contract_compilation_arguments)
set(_contract_compilation_object_lists)
foreach(_target IN LISTS _contract_library_targets)
   get_target_property(_owner "${_target}" FORGE_CONTRACT_LIBRARY_ID)
   if(NOT _owner)
      message(FATAL_ERROR "materialized contract library has no descriptor owner")
   endif()
   _forge_contract_component_key("${_owner}" _owner_key)
   _forge_contract_generate_object_list(
      "${_target}" "library-${_owner_key}" _object_list
   )
   list(
      APPEND _contract_compilation_arguments
      --library-compilation "${_owner}" "${_object_list}"
   )
   list(APPEND _contract_compilation_object_lists "${_object_list}")
endforeach()

set(_abigen_command
   "${FORGE_CONTRACT_ABIGEN}"
   --contract "${FORGE_CONTRACT_NAME}"
   --abi "${_abi}"
   --dispatch "${_dispatcher}"
   --depfile "${_depfile}"
   --contract-graph "${FORGE_CONTRACT_GRAPH_FILE}"
   --attribute-plugin "${FORGE_CONTRACT_ATTR_PLUGIN}"
   --sysroot "${CMAKE_SYSROOT}"
   --include "${_include}"
   --include "${FORGE_CONTRACT_SOURCE_ROOT}"
)
list(APPEND _abigen_command ${_contract_compilation_arguments})
foreach(_source IN LISTS FORGE_CONTRACT_COMPILE_CHECKS)
   list(APPEND _abigen_command --dependency-source "${_source}")
endforeach()
foreach(_include_directory IN LISTS _visible_library_module_bases)
   list(APPEND _abigen_command --include "${_include_directory}")
endforeach()
foreach(_module_path IN LISTS _module_search_paths)
   list(APPEND _abigen_command --module-path "${_module_path}")
endforeach()
foreach(_wrapper IN LISTS _implementation_wrappers)
   list(APPEND _abigen_command --source-wrapper "${_wrapper}")
endforeach()
if(FORGE_CONTRACT_RICARDIAN_CONTRACTS)
   list(APPEND _abigen_command --ricardian-contracts "${FORGE_CONTRACT_RICARDIAN_CONTRACTS}")
endif()
if(FORGE_CONTRACT_RICARDIAN_CLAUSES)
   list(APPEND _abigen_command --ricardian-clauses "${FORGE_CONTRACT_RICARDIAN_CLAUSES}")
endif()
list(APPEND _abigen_command ${FORGE_CONTRACT_SOURCES})
set(
   _abigen_dependencies
   forge_guest_contract
   "${FORGE_CONTRACT_ABIGEN}"
   "${FORGE_CONTRACT_ATTR_PLUGIN}"
   ${FORGE_CONTRACT_SOURCES}
   ${FORGE_CONTRACT_HEADERS}
   ${FORGE_CONTRACT_COMPILE_CHECKS}
   ${_contract_library_targets}
   ${_contract_library_source_paths}
   ${_contract_compilation_object_lists}
)
foreach(_ricardian_file FORGE_CONTRACT_RICARDIAN_CONTRACTS FORGE_CONTRACT_RICARDIAN_CLAUSES)
   if(NOT "${${_ricardian_file}}" STREQUAL "")
      list(APPEND _abigen_dependencies "${${_ricardian_file}}")
   endif()
endforeach()

add_custom_command(
   OUTPUT "${_abi}" "${_dispatcher}" ${_implementation_wrappers}
   COMMAND ${CMAKE_COMMAND} -E make_directory "${_generated}"
   COMMAND ${_abigen_command}
   DEPENDS ${_abigen_dependencies}
   DEPFILE "${_depfile}"
   COMMAND_EXPAND_LISTS
   VERBATIM
)

add_executable(
   ${FORGE_CONTRACT_TARGET}
   "${_dispatcher}"
   ${_implementation_wrappers}
)
set_source_files_properties(
   "${_dispatcher}" ${_implementation_wrappers}
   PROPERTIES OBJECT_DEPENDS "${FORGE_CONTRACT_ATTR_PLUGIN}"
)
target_link_libraries(
   ${FORGE_CONTRACT_TARGET}
   PRIVATE
      forge_guest_contract
      forge_guest_eosio
      ${_root_contract_targets}
)
target_include_directories(
   ${FORGE_CONTRACT_TARGET}
   PRIVATE
      "${_include}"
      "${FORGE_CONTRACT_SOURCE_ROOT}"
      ${_visible_library_module_bases}
)
target_compile_definitions(${FORGE_CONTRACT_TARGET} PRIVATE FORGE_CONTRACT_GUEST=1)
target_compile_options(
   ${FORGE_CONTRACT_TARGET}
   PRIVATE
      -fplugin=${FORGE_CONTRACT_ATTR_PLUGIN}
      -fno-exceptions
      -fno-rtti
      -fno-threadsafe-statics
      -ffreestanding
      -fvisibility=hidden
      -fno-ident
      -mcpu=mvp
      -Werror=return-type
      ${_contract_optimization}
      -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
      -ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.
      -ffile-prefix-map=${FORGE_CONTRACT_SOURCE_ROOT}=./source
)
foreach(_module_path IN LISTS _module_search_paths)
   target_compile_options(
      ${FORGE_CONTRACT_TARGET}
      PRIVATE "-fprebuilt-module-path=${_module_path}"
   )
endforeach()

if(FORGE_CONTRACT_COMPILE_CHECKS)
   add_library(${FORGE_CONTRACT_TARGET}_compile_checks OBJECT ${FORGE_CONTRACT_COMPILE_CHECKS})
   set_source_files_properties(
      ${FORGE_CONTRACT_COMPILE_CHECKS}
      PROPERTIES OBJECT_DEPENDS "${FORGE_CONTRACT_ATTR_PLUGIN}"
   )
   target_link_libraries(
      ${FORGE_CONTRACT_TARGET}_compile_checks
      PRIVATE
         forge_guest_contract
         forge_guest_eosio
         ${_root_contract_targets}
   )
   target_include_directories(
      ${FORGE_CONTRACT_TARGET}_compile_checks
      PRIVATE
         "${_include}"
         "${FORGE_CONTRACT_SOURCE_ROOT}"
         ${_visible_library_module_bases}
   )
   target_compile_definitions(${FORGE_CONTRACT_TARGET}_compile_checks PRIVATE FORGE_CONTRACT_GUEST=1)
   target_compile_options(
      ${FORGE_CONTRACT_TARGET}_compile_checks
      PRIVATE
         -fplugin=${FORGE_CONTRACT_ATTR_PLUGIN}
         -fno-exceptions
         -fno-rtti
         -fno-threadsafe-statics
         -ffreestanding
         -fvisibility=hidden
         -fno-ident
         -mcpu=mvp
         -Werror=return-type
         ${_contract_optimization}
         -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
         -ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.
         -ffile-prefix-map=${FORGE_CONTRACT_SOURCE_ROOT}=./source
   )
   foreach(_module_path IN LISTS _module_search_paths)
      target_compile_options(
         ${FORGE_CONTRACT_TARGET}_compile_checks
         PRIVATE "-fprebuilt-module-path=${_module_path}"
      )
   endforeach()
   add_dependencies(${FORGE_CONTRACT_TARGET} ${FORGE_CONTRACT_TARGET}_compile_checks)
endif()

set(_source_prefix_index 0)
foreach(_source IN LISTS FORGE_CONTRACT_SOURCES)
   get_filename_component(_source_directory "${_source}" DIRECTORY)
   target_compile_options(
      ${FORGE_CONTRACT_TARGET}
      PRIVATE "-ffile-prefix-map=${_source_directory}=./source-${_source_prefix_index}"
   )
   math(EXPR _source_prefix_index "${_source_prefix_index} + 1")
endforeach()
if(FORGE_CONTRACT_COMPILE_CHECKS)
   set(_compile_check_prefix_index 0)
   foreach(_source IN LISTS FORGE_CONTRACT_COMPILE_CHECKS)
      get_filename_component(_source_directory "${_source}" DIRECTORY)
      target_compile_options(
         ${FORGE_CONTRACT_TARGET}_compile_checks
         PRIVATE "-ffile-prefix-map=${_source_directory}=./compile-check-${_compile_check_prefix_index}"
      )
      math(EXPR _compile_check_prefix_index "${_compile_check_prefix_index} + 1")
   endforeach()
endif()
target_link_options(
   ${FORGE_CONTRACT_TARGET}
   PRIVATE
      -nostdlib
      -Wl,--no-entry
      -Wl,--export=apply
      -Wl,--export-if-defined=__forge_call
      -Wl,--export-memory
      -Wl,--stack-first
      -Wl,-z,stack-size=8192
      -Wl,--initial-memory=131072
      -Wl,--max-memory=16777216
      -Wl,--allow-undefined
      -Wl,--gc-sections
      -Wl,--strip-all
      -Wl,--no-merge-data-segments
)

find_library(_libcxx NAMES c++ PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
find_library(_libcxxabi NAMES c++abi PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
find_library(_libc NAMES c PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
find_library(_libm NAMES m PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
find_library(
   _builtins
   NAMES clang_rt.builtins-wasm32
   PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" "${CMAKE_SYSROOT}/lib/generic"
   NO_DEFAULT_PATH
)
if(NOT _libcxx)
   message(FATAL_ERROR "Forge Contract sysroot is missing libc++")
endif()
if(FORGE_CONTRACT_PROFILE STREQUAL "release" AND (NOT _libcxxabi OR NOT _builtins))
   message(FATAL_ERROR "release Forge Contract sysroot requires libc++abi and compiler-rt builtins")
endif()
set(_guest_libraries forge_guest_runtime "${_libcxx}")
foreach(_library _libcxxabi _libc _libm _builtins)
   if(${_library})
      list(APPEND _guest_libraries "${${_library}}")
   endif()
endforeach()
target_link_libraries(${FORGE_CONTRACT_TARGET} PRIVATE ${_guest_libraries})

set(_manifest_llvm_commit_args)
if(NOT FORGE_CONTRACT_LLVM_COMMIT STREQUAL "")
   list(APPEND _manifest_llvm_commit_args --llvm-commit "${FORGE_CONTRACT_LLVM_COMMIT}")
endif()

set_target_properties(
   ${FORGE_CONTRACT_TARGET} PROPERTIES
   OUTPUT_NAME "${FORGE_CONTRACT_TARGET}"
   SUFFIX ".wasm"
   RUNTIME_OUTPUT_DIRECTORY "${FORGE_CONTRACT_OUTPUT_DIR}"
)
foreach(_configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
   string(TOUPPER "${_configuration}" _configuration_upper)
   set_target_properties(
      ${FORGE_CONTRACT_TARGET} PROPERTIES
      "RUNTIME_OUTPUT_DIRECTORY_${_configuration_upper}" "${FORGE_CONTRACT_OUTPUT_DIR}"
   )
endforeach()

add_custom_command(
   TARGET ${FORGE_CONTRACT_TARGET}
   POST_BUILD
   COMMAND
      "${FORGE_CONTRACT_CHECK}"
      --wasm "$<TARGET_FILE:${FORGE_CONTRACT_TARGET}>"
      --abi "${_abi}"
      --imports "${FORGE_CONTRACT_DATA_DIR}/registry/intrinsics.json"
      --required-export apply
   COMMAND
      "${FORGE_CONTRACT_MANIFEST}"
      --wasm "$<TARGET_FILE:${FORGE_CONTRACT_TARGET}>"
      --abi "${_abi}"
      --imports "${FORGE_CONTRACT_DATA_DIR}/registry/intrinsics.json"
      --source-graph "${FORGE_CONTRACT_GRAPH_FILE}"
      --output "${_manifest}"
      --sdk-version "${FORGE_CONTRACT_SDK_VERSION}"
      --llvm-version "${FORGE_CONTRACT_LLVM_VERSION}"
      ${_manifest_llvm_commit_args}
      --sysroot-version "${FORGE_CONTRACT_SYSROOT_SCHEMA_VERSION}"
      --sysroot-hash "${FORGE_CONTRACT_SYSROOT_HASH}"
      --intrinsic-version "${FORGE_CONTRACT_INTRINSIC_VERSION}"
      --profile "${FORGE_CONTRACT_PROFILE}"
      --reproducible "${FORGE_CONTRACT_REPRODUCIBLE}"
   VERBATIM
)

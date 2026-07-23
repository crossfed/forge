cmake_minimum_required(VERSION 3.31)

project(ForgeContractConsumer LANGUAGES CXX)

foreach(_required
   FORGE_CONTRACT_TARGET
   FORGE_CONTRACT_NAME
   FORGE_CONTRACT_SOURCES_ENCODED
   FORGE_CONTRACT_GRAPH_FILE
   FORGE_CONTRACT_GRAPH_HASH
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
   message(FATAL_ERROR "Forge Contract library graph does not exist: ${FORGE_CONTRACT_GRAPH_FILE}")
endif()
file(SHA256 "${FORGE_CONTRACT_GRAPH_FILE}" _contract_graph_hash)
if(NOT _contract_graph_hash STREQUAL FORGE_CONTRACT_GRAPH_HASH)
   message(FATAL_ERROR "Forge Contract library graph hash mismatch")
endif()
include("${FORGE_CONTRACT_GRAPH_FILE}")
if(NOT FORGE_CONTRACT_GRAPH_VERSION EQUAL 1)
   message(FATAL_ERROR "unsupported Forge Contract library graph version")
endif()

string(REPLACE "|" ";" FORGE_CONTRACT_SOURCES "${FORGE_CONTRACT_SOURCES_ENCODED}")
string(REPLACE "|" ";" FORGE_CONTRACT_COMPILE_CHECKS "${FORGE_CONTRACT_COMPILE_CHECKS_ENCODED}")
string(REPLACE "|" ";" FORGE_CONTRACT_INCLUDE_DIRECTORIES "${FORGE_CONTRACT_INCLUDE_DIRECTORIES_ENCODED}")
set(_implementation_sources ${FORGE_CONTRACT_SOURCES})
list(REMOVE_AT _implementation_sources 0)

function(_forge_contract_append_source_graph_file path owner role logical physical)
   foreach(_value IN ITEMS "${owner}" "${role}" "${logical}" "${physical}")
      string(FIND "${_value}" "|" _pipe_index)
      string(FIND "${_value}" "\r" _carriage_return_index)
      string(FIND "${_value}" "\n" _newline_index)
      if(NOT _pipe_index EQUAL -1 OR NOT _carriage_return_index EQUAL -1 OR NOT _newline_index EQUAL -1)
         message(FATAL_ERROR "contract source graph value contains a reserved separator: ${_value}")
      endif()
   endforeach()
   file(APPEND "${path}" "F|${owner}|${role}|${logical}|${physical}\n")
endfunction()

function(_forge_contract_append_source_graph_edge path owner dependency)
   foreach(_value IN ITEMS "${owner}" "${dependency}")
      string(FIND "${_value}" "|" _pipe_index)
      string(FIND "${_value}" "\r" _carriage_return_index)
      string(FIND "${_value}" "\n" _newline_index)
      if(NOT _pipe_index EQUAL -1 OR NOT _carriage_return_index EQUAL -1 OR NOT _newline_index EQUAL -1)
         message(FATAL_ERROR "contract source graph edge contains a reserved separator: ${_value}")
      endif()
   endforeach()
   file(APPEND "${path}" "E|${owner}|${dependency}\n")
endfunction()

set(_source_graph "${CMAKE_CURRENT_BINARY_DIR}/contract-source-graph.txt")
set(_source_graph_inputs ${FORGE_CONTRACT_SOURCES} ${FORGE_CONTRACT_COMPILE_CHECKS})
set(_contract_graph_owner "contract:${FORGE_CONTRACT_NAME}")
file(WRITE "${_source_graph}" "FORGE_CONTRACT_SOURCE_GRAPH_V2\n")
set(_source_index 0)
foreach(_source IN LISTS FORGE_CONTRACT_SOURCES)
   get_filename_component(_name "${_source}" NAME)
   _forge_contract_append_source_graph_file(
      "${_source_graph}"
      "${_contract_graph_owner}"
      contract_source
      "contract/source-${_source_index}/${_name}"
      "${_source}"
   )
   math(EXPR _source_index "${_source_index} + 1")
endforeach()
set(_compile_check_index 0)
foreach(_source IN LISTS FORGE_CONTRACT_COMPILE_CHECKS)
   get_filename_component(_name "${_source}" NAME)
   _forge_contract_append_source_graph_file(
      "${_source_graph}"
      "${_contract_graph_owner}"
      compile_check
      "contract/compile-check-${_compile_check_index}/${_name}"
      "${_source}"
   )
   math(EXPR _compile_check_index "${_compile_check_index} + 1")
endforeach()
if(FORGE_CONTRACT_RICARDIAN_CONTRACTS)
   list(APPEND _source_graph_inputs "${FORGE_CONTRACT_RICARDIAN_CONTRACTS}")
   get_filename_component(_name "${FORGE_CONTRACT_RICARDIAN_CONTRACTS}" NAME)
   _forge_contract_append_source_graph_file(
      "${_source_graph}"
      "${_contract_graph_owner}"
      ricardian_contracts
      "contract/ricardian/contracts/${_name}"
      "${FORGE_CONTRACT_RICARDIAN_CONTRACTS}"
   )
endif()
if(FORGE_CONTRACT_RICARDIAN_CLAUSES)
   list(APPEND _source_graph_inputs "${FORGE_CONTRACT_RICARDIAN_CLAUSES}")
   get_filename_component(_name "${FORGE_CONTRACT_RICARDIAN_CLAUSES}" NAME)
   _forge_contract_append_source_graph_file(
      "${_source_graph}"
      "${_contract_graph_owner}"
      ricardian_clauses
      "contract/ricardian/clauses/${_name}"
      "${FORGE_CONTRACT_RICARDIAN_CLAUSES}"
   )
endif()
foreach(_dependency_id IN LISTS FORGE_CONTRACT_GRAPH_ROOT_LIBRARY_IDS)
   _forge_contract_append_source_graph_edge(
      "${_source_graph}" "${_contract_graph_owner}" "${_dependency_id}"
   )
endforeach()

set(_modules "${FORGE_CONTRACT_DATA_DIR}/modules")
set(_include "${FORGE_CONTRACT_DATA_DIR}/include")
set(_runtime_archive "${CMAKE_SYSROOT}/lib/libforge_guest_runtime.a")
set(_raw_archive "${CMAKE_SYSROOT}/lib/libforge_guest_raw.a")
set(_codec_base64_archive "${CMAKE_SYSROOT}/lib/libforge_guest_codec_base64.a")
set(_codec_base58_archive "${CMAKE_SYSROOT}/lib/libforge_guest_codec_base58.a")
set(_codec_hex_archive "${CMAKE_SYSROOT}/lib/libforge_guest_codec_hex.a")
set(_chain_protocol_archive "${CMAKE_SYSROOT}/lib/libforge_guest_chain_protocol.a")
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
set(_contract_optimization "$<$<NOT:$<CONFIG:Debug>>:-O3>")

add_library(forge_guest_raw STATIC)
target_sources(
   forge_guest_raw
   PUBLIC
      FILE_SET forge_guest_raw_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/raw/stream.cppm"
         "${_modules}/forge/raw/varint_value.cppm"
         "${_modules}/forge/raw/codec.cppm"
)
add_library(forge_guest_raw_implementation STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_raw_implementation PROPERTIES IMPORTED_LOCATION "${_raw_archive}")
target_link_libraries(forge_guest_raw PUBLIC forge_guest_raw_implementation)

add_library(forge_guest_codec_base64 STATIC)
target_sources(
   forge_guest_codec_base64
   PUBLIC
      FILE_SET forge_guest_codec_base64_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/codec/base64/exceptions.cppm"
         "${_modules}/forge/codec/base64/base64.cppm"
)
add_library(forge_guest_codec_base64_runtime STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_codec_base64_runtime PROPERTIES IMPORTED_LOCATION "${_codec_base64_archive}")
target_link_libraries(forge_guest_codec_base64 PUBLIC forge_guest_codec_base64_runtime)

add_library(forge_guest_codec_base58 STATIC)
target_sources(
   forge_guest_codec_base58
   PUBLIC
      FILE_SET forge_guest_codec_base58_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/codec/base58/exceptions.cppm"
         "${_modules}/forge/codec/base58/base58.cppm"
)
add_library(forge_guest_codec_base58_runtime STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_codec_base58_runtime PROPERTIES IMPORTED_LOCATION "${_codec_base58_archive}")
target_link_libraries(forge_guest_codec_base58 PUBLIC forge_guest_codec_base58_runtime)

add_library(forge_guest_codec_hex STATIC)
target_sources(
   forge_guest_codec_hex
   PUBLIC
      FILE_SET forge_guest_codec_hex_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/codec/hex/exceptions.cppm"
         "${_modules}/forge/codec/hex/hex.cppm"
)
add_library(forge_guest_codec_hex_runtime STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_codec_hex_runtime PROPERTIES IMPORTED_LOCATION "${_codec_hex_archive}")
target_link_libraries(forge_guest_codec_hex PUBLIC forge_guest_codec_hex_runtime)

add_library(forge_guest_crypto STATIC)
target_sources(
   forge_guest_crypto
   PUBLIC
      FILE_SET forge_guest_crypto_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/crypto/digest/sha256_value.cppm"
         "${_modules}/forge/crypto/digest/sha256.cppm"
         "${_modules}/forge/crypto/digest/sha512_value.cppm"
         "${_modules}/forge/crypto/digest/sha512.cppm"
         "${_modules}/forge/crypto/digest/ripemd160_value.cppm"
         "${_modules}/forge/crypto/digest/ripemd160.cppm"
         "${_modules}/forge/crypto/asymmetric/values.cppm"
         "${_modules}/forge/crypto/asymmetric/asymmetric.cppm"
         "${_modules}/forge/crypto/bls/bls_values.cppm"
)
target_link_libraries(forge_guest_crypto PUBLIC forge_guest_raw)

add_library(forge_guest_chain_protocol STATIC)
target_sources(
   forge_guest_chain_protocol
   PUBLIC
      FILE_SET forge_guest_chain_protocol_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/chain/protocol/values.cppm"
         "${_modules}/forge/chain/protocol/time.cppm"
         "${_modules}/forge/chain/protocol/types_value.cppm"
         "${_modules}/forge/chain/protocol/types.cppm"
         "${_modules}/forge/chain/protocol/fixed_key_value.cppm"
         "${_modules}/forge/chain/protocol/fixed_key.cppm"
         "${_modules}/forge/chain/protocol/action_value.cppm"
         "${_modules}/forge/chain/protocol/action.cppm"
         "${_modules}/forge/chain/protocol/transaction_value.cppm"
         "${_modules}/forge/chain/protocol/transaction.cppm"
         "${_modules}/forge/chain/protocol/authority_value.cppm"
         "${_modules}/forge/chain/protocol/authority.cppm"
         "${_modules}/forge/chain/protocol/producer_schedule_value.cppm"
         "${_modules}/forge/chain/protocol/producer_schedule.cppm"
         "${_modules}/forge/chain/protocol/producer_authority.cppm"
         "${_modules}/forge/chain/protocol/system_value.cppm"
         "${_modules}/forge/chain/protocol/system.cppm"
         "${_modules}/forge/chain/protocol/code_hash_result.cppm"
         "${_modules}/forge/chain/protocol/blockchain_parameters.cppm"
         "${_modules}/forge/chain/protocol/kv_parameters.cppm"
         "${_modules}/forge/chain/protocol/finalizer_authority.cppm"
         "${_modules}/forge/chain/protocol/finalizer_policy.cppm"
         "${_modules}/forge/chain/protocol/hash_id.cppm"
         "${_modules}/forge/chain/protocol/call_access_mode.cppm"
         "${_modules}/forge/chain/protocol/call_data_header.cppm"
)
add_library(forge_guest_chain_protocol_implementation STATIC IMPORTED GLOBAL)
set_target_properties(
   forge_guest_chain_protocol_implementation
   PROPERTIES IMPORTED_LOCATION "${_chain_protocol_archive}"
)
target_link_libraries(
   forge_guest_chain_protocol
   PUBLIC
      forge_guest_chain_protocol_implementation
      forge_guest_raw
      forge_guest_crypto
)

foreach(
   _archive
   IN ITEMS
      "${_runtime_archive}"
      "${_raw_archive}"
      "${_codec_base64_archive}"
      "${_codec_base58_archive}"
      "${_codec_hex_archive}"
      "${_chain_protocol_archive}"
      "${_contract_archive}"
)
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
      forge_guest_raw
      forge_guest_chain_protocol
      forge_guest_codec_base64
      forge_guest_codec_base58
      forge_guest_codec_hex
)

add_library(forge_guest_eosio INTERFACE)
target_include_directories(forge_guest_eosio INTERFACE "${_include}")
target_link_libraries(forge_guest_eosio INTERFACE forge_guest_contract)

foreach(
   _target
   forge_guest_raw
   forge_guest_codec_base64
   forge_guest_codec_base58
   forge_guest_codec_hex
   forge_guest_crypto
   forge_guest_chain_protocol
   forge_guest_contract
)
   target_include_directories(${_target} PRIVATE "${_include}")
   target_compile_definitions(${_target} PRIVATE FORGE_CONTRACT_GUEST=1)
   target_compile_options(
      ${_target}
      PRIVATE
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

function(_forge_contract_configure_downstream_target target)
   target_compile_definitions(${target} PRIVATE FORGE_CONTRACT_GUEST=1)
   target_compile_options(
      ${target}
      PRIVATE
         -fno-exceptions
         -fno-rtti
         -fno-threadsafe-statics
         -ffreestanding
         -fvisibility=hidden
         -fno-ident
         -mcpu=mvp
         ${_contract_optimization}
   )
endfunction()

set(_contract_library_targets)
if(FORGE_CONTRACT_GRAPH_LIBRARY_COUNT GREATER 0)
   math(EXPR _contract_library_last "${FORGE_CONTRACT_GRAPH_LIBRARY_COUNT} - 1")
   foreach(_index RANGE 0 ${_contract_library_last})
      set(_prefix "FORGE_CONTRACT_GRAPH_LIBRARY_${_index}")
      set(_id "${${_prefix}_ID}")
      string(SHA256 _id_hash "${_id}")
      string(SUBSTRING "${_id_hash}" 0 16 _id_hash)
      set(_target "forge_contract_library_${_id_hash}")
      set("_forge_contract_library_target_${_id_hash}" "${_target}")

      set(_dependencies ${${_prefix}_GUEST_DEPENDENCIES})
      foreach(_dependency_id IN LISTS ${_prefix}_DEPENDENCY_IDS)
         string(SHA256 _dependency_hash "${_dependency_id}")
         string(SUBSTRING "${_dependency_hash}" 0 16 _dependency_hash)
         if(NOT DEFINED _forge_contract_library_target_${_dependency_hash})
            message(FATAL_ERROR "contract library dependency is missing from graph: ${_dependency_id}")
         endif()
         list(APPEND _dependencies "${_forge_contract_library_target_${_dependency_hash}}")
      endforeach()

      add_library(${_target} STATIC)
      target_sources(
         ${_target}
         PUBLIC
            FILE_SET forge_contract_modules TYPE CXX_MODULES
            BASE_DIRS ${${_prefix}_MODULE_BASES}
            FILES ${${_prefix}_MODULE_PATHS}
      )
      if(${_prefix}_SOURCE_PATHS)
         target_sources(${_target} PRIVATE ${${_prefix}_SOURCE_PATHS})
      endif()
      if(${_prefix}_PUBLIC_HEADER_PATHS OR ${_prefix}_PRIVATE_HEADER_PATHS)
         target_sources(
            ${_target}
            PRIVATE
               ${${_prefix}_PUBLIC_HEADER_PATHS}
               ${${_prefix}_PRIVATE_HEADER_PATHS}
         )
      endif()
      target_include_directories(${_target} PUBLIC ${${_prefix}_MODULE_BASES})
      if(_dependencies)
         target_link_libraries(${_target} PUBLIC ${_dependencies})
      endif()
      _forge_contract_configure_downstream_target(${_target})
      list(APPEND _contract_library_targets "${_target}")

      foreach(_role module implementation public_header private_header)
         if(_role STREQUAL "module")
            set(_logical_values ${${_prefix}_MODULE_LOGICAL})
            set(_physical_values ${${_prefix}_MODULE_PATHS})
         elseif(_role STREQUAL "implementation")
            set(_logical_values ${${_prefix}_SOURCE_LOGICAL})
            set(_physical_values ${${_prefix}_SOURCE_PATHS})
         elseif(_role STREQUAL "public_header")
            set(_logical_values ${${_prefix}_PUBLIC_HEADER_LOGICAL})
            set(_physical_values ${${_prefix}_PUBLIC_HEADER_PATHS})
         else()
            set(_logical_values ${${_prefix}_PRIVATE_HEADER_LOGICAL})
            set(_physical_values ${${_prefix}_PRIVATE_HEADER_PATHS})
         endif()
         list(LENGTH _logical_values _value_count)
         if(_value_count GREATER 0)
            math(EXPR _value_last "${_value_count} - 1")
            foreach(_value_index RANGE 0 ${_value_last})
               list(GET _logical_values ${_value_index} _logical)
               list(GET _physical_values ${_value_index} _physical)
               list(APPEND _source_graph_inputs "${_physical}")
               _forge_contract_append_source_graph_file(
                  "${_source_graph}" "${_id}" "${_role}" "${_logical}" "${_physical}"
               )
            endforeach()
         endif()
      endforeach()
      foreach(_dependency_id IN LISTS ${_prefix}_DEPENDENCY_IDS)
         _forge_contract_append_source_graph_edge(
            "${_source_graph}" "${_id}" "${_dependency_id}"
         )
      endforeach()
   endforeach()
endif()

set(_module_configuration_directory "")
if(CMAKE_CONFIGURATION_TYPES)
   set(_module_configuration_directory "$<CONFIG>/")
endif()
set(
   _module_targets
   forge_guest_raw
   forge_guest_codec_base64
   forge_guest_codec_base58
   forge_guest_codec_hex
   forge_guest_crypto
   forge_guest_chain_protocol
   forge_guest_contract
   ${_contract_library_targets}
)
set(_module_search_paths)
foreach(_module_target IN LISTS _module_targets)
   list(
      APPEND _module_search_paths
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${_module_target}.dir/${_module_configuration_directory}"
   )
endforeach()

set(_abigen_command
   "${FORGE_CONTRACT_ABIGEN}"
   --contract "${FORGE_CONTRACT_NAME}"
   --abi "${_abi}"
   --dispatch "${_dispatcher}"
   --depfile "${_depfile}"
   --attribute-plugin "${FORGE_CONTRACT_ATTR_PLUGIN}"
   --sysroot "${CMAKE_SYSROOT}"
   --include "${_include}"
)
foreach(_include_directory IN LISTS FORGE_CONTRACT_INCLUDE_DIRECTORIES)
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
   ${_contract_library_targets}
   "${FORGE_CONTRACT_ABIGEN}"
   "${FORGE_CONTRACT_ATTR_PLUGIN}"
   ${FORGE_CONTRACT_SOURCES}
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
set_property(
   TARGET ${FORGE_CONTRACT_TARGET}
   APPEND
   PROPERTY LINK_DEPENDS "${_source_graph};${_source_graph_inputs}"
)
target_link_libraries(
   ${FORGE_CONTRACT_TARGET}
   PRIVATE
      forge_guest_contract
      forge_guest_eosio
      ${FORGE_CONTRACT_GRAPH_ROOT_GUEST_DEPENDENCIES}
      ${_contract_library_targets}
)
target_include_directories(
   ${FORGE_CONTRACT_TARGET}
   PRIVATE
      "${_include}"
      ${FORGE_CONTRACT_INCLUDE_DIRECTORIES}
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
)

if(FORGE_CONTRACT_COMPILE_CHECKS)
   add_library(${FORGE_CONTRACT_TARGET}_compile_checks OBJECT ${FORGE_CONTRACT_COMPILE_CHECKS})
   target_link_libraries(
      ${FORGE_CONTRACT_TARGET}_compile_checks
      PRIVATE
         forge_guest_contract
         forge_guest_eosio
         ${FORGE_CONTRACT_GRAPH_ROOT_GUEST_DEPENDENCIES}
         ${_contract_library_targets}
   )
   target_include_directories(
      ${FORGE_CONTRACT_TARGET}_compile_checks
      PRIVATE
         "${_include}"
         ${FORGE_CONTRACT_INCLUDE_DIRECTORIES}
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
   )
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
      --source-graph "${_source_graph}"
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

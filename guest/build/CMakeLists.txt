cmake_minimum_required(VERSION 3.31)

project(ForgeContractConsumer LANGUAGES CXX)

foreach(_required
   FORGE_CONTRACT_NAME
   FORGE_CONTRACT_SOURCES_ENCODED
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

string(REPLACE "|" ";" FORGE_CONTRACT_SOURCES "${FORGE_CONTRACT_SOURCES_ENCODED}")
string(REPLACE "|" ";" FORGE_CONTRACT_COMPILE_CHECKS "${FORGE_CONTRACT_COMPILE_CHECKS_ENCODED}")
set(_implementation_sources ${FORGE_CONTRACT_SOURCES})
list(REMOVE_AT _implementation_sources 0)

set(_modules "${FORGE_CONTRACT_DATA_DIR}/modules")
set(_include "${FORGE_CONTRACT_DATA_DIR}/include")
set(_runtime "${FORGE_CONTRACT_DATA_DIR}/runtime")
set(_runtime_archive "${CMAKE_SYSROOT}/lib/libforge_guest_runtime.a")
set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_abi "${FORGE_CONTRACT_OUTPUT_DIR}/${FORGE_CONTRACT_NAME}.abi")
set(_dispatcher "${_generated}/${FORGE_CONTRACT_NAME}.dispatcher.cpp")
set(_implementation_wrappers)
set(_wrapper_index 1)
foreach(_source IN LISTS _implementation_sources)
   list(APPEND _implementation_wrappers "${_generated}/${FORGE_CONTRACT_NAME}.source-${_wrapper_index}.cpp")
   math(EXPR _wrapper_index "${_wrapper_index} + 1")
endforeach()
set(_depfile "${_generated}/${FORGE_CONTRACT_NAME}.abi.d")
set(_wasm "${FORGE_CONTRACT_OUTPUT_DIR}/${FORGE_CONTRACT_NAME}.wasm")
set(_manifest "${FORGE_CONTRACT_OUTPUT_DIR}/${FORGE_CONTRACT_NAME}.contract.json")

add_library(forge_guest_raw STATIC "${_runtime}/stream.cpp" "${_runtime}/codec.cpp")
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

add_library(forge_guest_crypto STATIC)
target_sources(
   forge_guest_crypto
   PUBLIC
      FILE_SET forge_guest_crypto_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/crypto/sha256_value.cppm"
         "${_modules}/forge/crypto/sha256.cppm"
         "${_modules}/forge/crypto/sha512_value.cppm"
         "${_modules}/forge/crypto/sha512.cppm"
         "${_modules}/forge/crypto/ripemd160_value.cppm"
         "${_modules}/forge/crypto/ripemd160.cppm"
         "${_modules}/forge/crypto/asymmetric_value.cppm"
         "${_modules}/forge/crypto/asymmetric.cppm"
         "${_modules}/forge/crypto/bls_values.cppm"
)
target_link_libraries(forge_guest_crypto PUBLIC forge_guest_raw)

add_library(forge_guest_chain_protocol STATIC "${_runtime}/values.cpp" "${_runtime}/time.cpp")
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
         "${_modules}/forge/chain/protocol/producer_authority.cppm"
         "${_modules}/forge/chain/protocol/system_value.cppm"
         "${_modules}/forge/chain/protocol/system.cppm"
)
target_link_libraries(forge_guest_chain_protocol PUBLIC forge_guest_raw forge_guest_crypto)

if(NOT EXISTS "${_runtime_archive}")
   message(FATAL_ERROR "Forge Contract sysroot is missing ${_runtime_archive}")
endif()
add_library(forge_guest_runtime STATIC IMPORTED GLOBAL)
set_target_properties(forge_guest_runtime PROPERTIES IMPORTED_LOCATION "${_runtime_archive}")

add_library(forge_guest_contract STATIC "${_runtime}/dispatcher.cpp")
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
         "${_modules}/forge/contract/base64.cppm"
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
)
target_link_libraries(
   forge_guest_contract
   PUBLIC
      forge_guest_raw
      forge_guest_chain_protocol
)

add_library(forge_guest_eosio INTERFACE)
target_include_directories(forge_guest_eosio INTERFACE "${_include}")
target_link_libraries(forge_guest_eosio INTERFACE forge_guest_contract)

foreach(_target forge_guest_raw forge_guest_crypto forge_guest_chain_protocol forge_guest_contract)
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
   )
endforeach()

set(_module_arguments
   "forge.raw.stream=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_raw.dir/forge.raw.stream.pcm"
   "forge.raw.varint_value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_raw.dir/forge.raw.varint_value.pcm"
   "forge.raw.codec=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_raw.dir/forge.raw.codec.pcm"
   "forge.crypto.sha256:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.sha256-value.pcm"
   "forge.crypto.sha256=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.sha256.pcm"
   "forge.crypto.sha512:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.sha512-value.pcm"
   "forge.crypto.sha512=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.sha512.pcm"
   "forge.crypto.ripemd160:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.ripemd160-value.pcm"
   "forge.crypto.ripemd160=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.ripemd160.pcm"
   "forge.crypto.asymmetric:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.asymmetric-value.pcm"
   "forge.crypto.asymmetric=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.asymmetric.pcm"
   "forge.crypto.bls.values=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_crypto.dir/forge.crypto.bls.values.pcm"
   "forge.chain.protocol.values=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.values.pcm"
   "forge.chain.protocol.time=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.time.pcm"
   "forge.chain.protocol.types:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.types-value.pcm"
   "forge.chain.protocol.types=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.types.pcm"
   "forge.chain.protocol.fixed_key:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.fixed_key-value.pcm"
   "forge.chain.protocol.fixed_key=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.fixed_key.pcm"
   "forge.chain.protocol.action:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.action-value.pcm"
   "forge.chain.protocol.action=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.action.pcm"
   "forge.chain.protocol.transaction:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.transaction-value.pcm"
   "forge.chain.protocol.transaction=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.transaction.pcm"
   "forge.chain.protocol.authority:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.authority-value.pcm"
   "forge.chain.protocol.authority=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.authority.pcm"
   "forge.chain.protocol.producer_authority=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.producer_authority.pcm"
   "forge.chain.protocol.system:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.system-value.pcm"
   "forge.chain.protocol.system=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.system.pcm"
   "forge.contract.intrinsics=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.intrinsics.pcm"
   "forge.contract=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.pcm"
   "forge.contract.datastream=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.datastream.pcm"
   "forge.contract.varint=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.varint.pcm"
   "forge.contract.fixed_bytes=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.fixed_bytes.pcm"
   "forge.contract.binary_extension=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.binary_extension.pcm"
   "forge.contract.ignore=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.ignore.pcm"
   "forge.contract.hash_id=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.hash_id.pcm"
   "forge.contract.action=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.action.pcm"
   "forge.contract.base64=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.base64.pcm"
   "forge.contract.transaction=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.transaction.pcm"
   "forge.contract.system=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.system.pcm"
   "forge.contract.deferred_transaction=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.deferred_transaction.pcm"
   "forge.contract.authorization=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.authorization.pcm"
   "forge.contract.bitset=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.bitset.pcm"
   "forge.contract.call=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.call.pcm"
   "forge.contract.crypto=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.crypto.pcm"
   "forge.contract.crypto_bls_ext=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.crypto_bls_ext.pcm"
   "forge.contract.crypto_ext=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.crypto_ext.pcm"
   "forge.contract.instant_finality=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.instant_finality.pcm"
   "forge.contract.key=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.key.pcm"
   "forge.contract.powers=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.powers.pcm"
   "forge.contract.print=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.print.pcm"
   "forge.contract.privileged=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.privileged.pcm"
   "forge.contract.producer_schedule=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.producer_schedule.pcm"
   "forge.contract.rope=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.rope.pcm"
   "forge.contract.string=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.string.pcm"
   "forge.contract.dispatcher=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.dispatcher.pcm"
   "forge.contract.multi_index=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.multi_index.pcm"
   "forge.contract.singleton=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.singleton.pcm"
)
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
foreach(_module IN LISTS _module_arguments)
   list(APPEND _abigen_command --module-file "${_module}")
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
   ${FORGE_CONTRACT_NAME}
   "${_dispatcher}"
   ${_implementation_wrappers}
)
target_link_libraries(${FORGE_CONTRACT_NAME} PRIVATE forge_guest_contract forge_guest_eosio)
target_include_directories(${FORGE_CONTRACT_NAME} PRIVATE "${_include}")
target_compile_definitions(${FORGE_CONTRACT_NAME} PRIVATE FORGE_CONTRACT_GUEST=1)
target_compile_options(
   ${FORGE_CONTRACT_NAME}
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
      -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
      -ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.
)

if(FORGE_CONTRACT_COMPILE_CHECKS)
   add_library(${FORGE_CONTRACT_NAME}_compile_checks OBJECT ${FORGE_CONTRACT_COMPILE_CHECKS})
   target_link_libraries(
      ${FORGE_CONTRACT_NAME}_compile_checks
      PRIVATE
         forge_guest_contract
         forge_guest_eosio
   )
   target_include_directories(${FORGE_CONTRACT_NAME}_compile_checks PRIVATE "${_include}")
   target_compile_definitions(${FORGE_CONTRACT_NAME}_compile_checks PRIVATE FORGE_CONTRACT_GUEST=1)
   target_compile_options(
      ${FORGE_CONTRACT_NAME}_compile_checks
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
         -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
         -ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.
   )
   add_dependencies(${FORGE_CONTRACT_NAME} ${FORGE_CONTRACT_NAME}_compile_checks)
endif()

set(_source_prefix_index 0)
foreach(_source IN LISTS FORGE_CONTRACT_SOURCES)
   get_filename_component(_source_directory "${_source}" DIRECTORY)
   target_compile_options(
      ${FORGE_CONTRACT_NAME}
      PRIVATE "-ffile-prefix-map=${_source_directory}=./source-${_source_prefix_index}"
   )
   math(EXPR _source_prefix_index "${_source_prefix_index} + 1")
endforeach()
if(FORGE_CONTRACT_COMPILE_CHECKS)
   set(_compile_check_prefix_index 0)
   foreach(_source IN LISTS FORGE_CONTRACT_COMPILE_CHECKS)
      get_filename_component(_source_directory "${_source}" DIRECTORY)
      target_compile_options(
         ${FORGE_CONTRACT_NAME}_compile_checks
         PRIVATE "-ffile-prefix-map=${_source_directory}=./compile-check-${_compile_check_prefix_index}"
      )
      math(EXPR _compile_check_prefix_index "${_compile_check_prefix_index} + 1")
   endforeach()
endif()
target_link_options(
   ${FORGE_CONTRACT_NAME}
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
      -Wl,--strip-all
)

find_library(_libcxx NAMES c++ PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
find_library(_libcxxabi NAMES c++abi PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
find_library(_libc NAMES c PATHS "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/lib/wasm32" NO_DEFAULT_PATH)
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
foreach(_library _libcxxabi _libc _builtins)
   if(${_library})
      list(APPEND _guest_libraries "${${_library}}")
   endif()
endforeach()
target_link_libraries(${FORGE_CONTRACT_NAME} PRIVATE ${_guest_libraries})

set(_manifest_llvm_commit_args)
if(NOT FORGE_CONTRACT_LLVM_COMMIT STREQUAL "")
   list(APPEND _manifest_llvm_commit_args --llvm-commit "${FORGE_CONTRACT_LLVM_COMMIT}")
endif()

set_target_properties(
   ${FORGE_CONTRACT_NAME} PROPERTIES
   OUTPUT_NAME "${FORGE_CONTRACT_NAME}"
   SUFFIX ".wasm"
   RUNTIME_OUTPUT_DIRECTORY "${FORGE_CONTRACT_OUTPUT_DIR}"
)

add_custom_command(
   TARGET ${FORGE_CONTRACT_NAME}
   POST_BUILD
   COMMAND
      "${FORGE_CONTRACT_CHECK}"
      --wasm "${_wasm}"
      --abi "${_abi}"
      --imports "${FORGE_CONTRACT_DATA_DIR}/registry/intrinsics.json"
      --required-export apply
   COMMAND
      "${FORGE_CONTRACT_MANIFEST}"
      --wasm "${_wasm}"
      --abi "${_abi}"
      --imports "${FORGE_CONTRACT_DATA_DIR}/registry/intrinsics.json"
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

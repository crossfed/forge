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
set(_implementation_sources ${FORGE_CONTRACT_SOURCES})
list(REMOVE_AT _implementation_sources 0)

set(_modules "${FORGE_CONTRACT_DATA_DIR}/modules")
set(_include "${FORGE_CONTRACT_DATA_DIR}/include")
set(_runtime "${FORGE_CONTRACT_DATA_DIR}/runtime")
set(_runtime_archive "${CMAKE_SYSROOT}/lib/libforge_guest_runtime.a")
set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_abi "${FORGE_CONTRACT_OUTPUT_DIR}/${FORGE_CONTRACT_NAME}.abi")
set(_dispatcher "${_generated}/${FORGE_CONTRACT_NAME}.dispatcher.cpp")
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

add_library(forge_guest_chain_protocol STATIC "${_runtime}/values.cpp")
target_sources(
   forge_guest_chain_protocol
   PUBLIC
      FILE_SET forge_guest_chain_protocol_modules TYPE CXX_MODULES
      BASE_DIRS "${_modules}"
      FILES
         "${_modules}/forge/chain/protocol/values.cppm"
         "${_modules}/forge/chain/protocol/fixed_key_value.cppm"
         "${_modules}/forge/chain/protocol/fixed_key.cppm"
)
target_link_libraries(forge_guest_chain_protocol PUBLIC forge_guest_raw)

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

foreach(_target forge_guest_raw forge_guest_chain_protocol forge_guest_contract)
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
   "forge.chain.protocol.values=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.values.pcm"
   "forge.chain.protocol.fixed_key:value=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.fixed_key-value.pcm"
   "forge.chain.protocol.fixed_key=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_chain_protocol.dir/forge.chain.protocol.fixed_key.pcm"
   "forge.contract.intrinsics=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.intrinsics.pcm"
   "forge.contract=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/forge_guest_contract.dir/forge.contract.pcm"
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
if(FORGE_CONTRACT_RICARDIAN_CONTRACTS)
   list(APPEND _abigen_command --ricardian-contracts "${FORGE_CONTRACT_RICARDIAN_CONTRACTS}")
endif()
if(FORGE_CONTRACT_RICARDIAN_CLAUSES)
   list(APPEND _abigen_command --ricardian-clauses "${FORGE_CONTRACT_RICARDIAN_CLAUSES}")
endif()
list(APPEND _abigen_command ${FORGE_CONTRACT_SOURCES})
set(_abigen_dependencies forge_guest_contract ${FORGE_CONTRACT_SOURCES})
foreach(_ricardian_file FORGE_CONTRACT_RICARDIAN_CONTRACTS FORGE_CONTRACT_RICARDIAN_CLAUSES)
   if(NOT "${${_ricardian_file}}" STREQUAL "")
      list(APPEND _abigen_dependencies "${${_ricardian_file}}")
   endif()
endforeach()

add_custom_command(
   OUTPUT "${_abi}" "${_dispatcher}"
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
   ${_implementation_sources}
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
set(_source_prefix_index 0)
foreach(_source IN LISTS FORGE_CONTRACT_SOURCES)
   get_filename_component(_source_directory "${_source}" DIRECTORY)
   target_compile_options(
      ${FORGE_CONTRACT_NAME}
      PRIVATE "-ffile-prefix-map=${_source_directory}=./source-${_source_prefix_index}"
   )
   math(EXPR _source_prefix_index "${_source_prefix_index} + 1")
endforeach()
target_link_options(
   ${FORGE_CONTRACT_NAME}
   PRIVATE
      -nostdlib
      -Wl,--no-entry
      -Wl,--export=apply
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

add_library(
   forge_contract_abi STATIC
   command.cpp
   generator.cpp
)
forge_target_modules_at(forge_contract_abi contract/abi)

target_include_directories(forge_contract_abi PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_include_directories(forge_contract_abi SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_compile_definitions(forge_contract_abi PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(
   forge_contract_abi
   PUBLIC
      forge_chain_protocol
      forge_codec_json
   PRIVATE
      clang-cpp
      LLVM
)

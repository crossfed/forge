add_library(
   forge_contract_validation STATIC
   command.cpp
   validator.cpp
)
forge_target_modules_at(forge_contract_validation contract/validation)

target_include_directories(forge_contract_validation PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_contract_validation
   PUBLIC
      forge_chain_protocol
      forge_codec_json
      forge_vm_wasm_interpret
)

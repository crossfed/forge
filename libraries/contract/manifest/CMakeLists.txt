add_library(
   forge_contract_manifest STATIC
   command.cpp
   generator.cpp
)
forge_target_modules_at(forge_contract_manifest contract/manifest)

target_include_directories(forge_contract_manifest PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_contract_manifest
   PUBLIC
      forge_codec_json
      forge_contract_graph
      forge_crypto_digest
      forge_vm_wasm
)

add_library(
   forge_tooling_manifest STATIC
   command.cpp
   generator.cpp
)
forge_target_modules_at(forge_tooling_manifest tooling/manifest)

target_include_directories(forge_tooling_manifest PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_tooling_manifest
   PUBLIC
      forge_codec_json
      forge_crypto_digest
      forge_vm_wasm_interpret
)

add_library(
   forge_tooling_validation STATIC
   command.cpp
   validator.cpp
)
forge_target_modules_at(forge_tooling_validation tooling/validation)

target_include_directories(forge_tooling_validation PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_tooling_validation
   PUBLIC
      forge_chain_protocol
      forge_codec_json
      forge_vm_wasm_interpret
)

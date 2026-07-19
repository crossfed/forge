add_library(
   forge_chain_protocol STATIC
   values.cpp
   time.cpp
   types.cpp
   action.cpp
   action_receipt.cpp
   transaction.cpp
   block.cpp
   abi.cpp
   authority.cpp
   system.cpp
)
forge_target_modules_at(forge_chain_protocol chain/protocol)

target_include_directories(forge_chain_protocol PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain_protocol PUBLIC
      forge_chain_core
      forge_compression
      forge_raw
      forge_variant
      forge_crypto
)

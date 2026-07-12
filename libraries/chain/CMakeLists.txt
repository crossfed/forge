add_library(
   forge_chain STATIC
   types.cpp
   merkle.cpp
   transaction.cpp
   block.cpp
   abi.cpp
   authority.cpp
   system.cpp
)
forge_target_modules(forge_chain chain)

target_include_directories(forge_chain PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain PUBLIC
      forge_compression
      forge_raw
      forge_variant
      forge_crypto
)

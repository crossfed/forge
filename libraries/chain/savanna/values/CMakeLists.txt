add_library(forge_chain_savanna_values STATIC values.cpp)
target_sources(
   forge_chain_savanna_values
   PUBLIC
      FILE_SET CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
      FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/forge/chain/savanna/values.cppm"
)
target_compile_features(forge_chain_savanna_values PUBLIC cxx_std_23)
target_include_directories(forge_chain_savanna_values PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_chain_savanna_values PUBLIC
      Boost::headers
      forge_crypto_bls
      forge_crypto_bls_values
      forge_crypto_digest
      forge_raw
      forge_variant
)
forge_target_contract_guest_component(
   forge_chain_savanna_values
   ID forge.chain.savanna.values
)

add_library(
   forge_chain_transaction STATIC
   builder.cpp
   signing.cpp
)
forge_target_modules_at(forge_chain_transaction chain/transaction)

target_include_directories(forge_chain_transaction PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain_transaction PUBLIC
      Boost::headers
      forge_chain_protocol
      forge_crypto_signer
      forge_exceptions
)

forge_add_contract_library(
   product_chain_values
   ID product.chain.values
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/product/chain/values.cppm
   PUBLIC_LIBRARIES Forge::forge_chain_protocol
)

forge_add_contract_library(
   product_chain_limits
   ID product.chain.limits
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/product/chain/limits.cppm
   SOURCES limits.cpp
   PUBLIC_LIBRARIES Forge::forge_crypto_digest
)

forge_add_contract_library(
   product_chain_protocol
   ID product.chain.protocol
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/product/chain/protocol.cppm
   SOURCES protocol.cpp
   PUBLIC_LIBRARIES
      Forge::forge_chain_protocol
      Forge::forge_raw
      product_chain_values
   PRIVATE_LIBRARIES product_chain_limits
)

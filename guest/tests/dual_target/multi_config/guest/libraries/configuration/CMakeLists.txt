forge_add_contract_library(
   product_contract_configuration
   ID product.contract.configuration
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/product/contract/configuration.cppm
   SOURCES configuration.cpp
   PUBLIC_LIBRARIES
      Forge::forge_chain_protocol
      Forge::forge_contract_runtime
)

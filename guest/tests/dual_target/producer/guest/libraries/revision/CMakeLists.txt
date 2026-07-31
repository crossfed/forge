forge_add_contract_library(
   product_contract_revision
   ID product.contract.revision
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/product/contract/revision.cppm
   SOURCES revision.cpp
   PUBLIC_LIBRARIES
      Forge::forge_contract_runtime
      product_chain_protocol
   PRIVATE_LIBRARIES product_contract_storage
)

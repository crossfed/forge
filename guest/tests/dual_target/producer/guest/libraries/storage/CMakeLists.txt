forge_add_contract_library(
   product_contract_storage
   ID product.contract.storage
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/product/contract/storage.cppm
   SOURCES storage.cpp
   PUBLIC_LIBRARIES product_chain_protocol
)

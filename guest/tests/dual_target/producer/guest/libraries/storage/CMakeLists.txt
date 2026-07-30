forge_add_contract_library(
   product_contract_storage
   ID product.contract.storage
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES include/product/contract/storage.cppm
   SOURCES storage.cpp
   PUBLIC_LIBRARIES protocol
)

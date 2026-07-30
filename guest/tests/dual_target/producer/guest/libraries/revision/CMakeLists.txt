forge_add_contract_library(
   product_contract_revision
   ID product.contract.revision
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES include/product/contract/revision.cppm
   SOURCES revision.cpp
   PUBLIC_LIBRARIES
      Forge::forge_contract_runtime
      protocol
   PRIVATE_LIBRARIES product_contract_storage
)

forge_add_contract_library(
   values
   ID product.chain.values
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES include/product/chain/values.cppm
   PUBLIC_HEADERS include/product/chain/version.hpp
   PUBLIC_LIBRARIES Forge::forge_chain_protocol
)

forge_add_contract_library(
   limits
   ID product.chain.limits
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES include/product/chain/limits.cppm
   SOURCES limits.cpp
   PUBLIC_LIBRARIES Forge::forge_crypto_digest
)

forge_add_contract_library(
   protocol
   ID product.chain.protocol
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES include/product/chain/protocol.cppm
   SOURCES protocol.cpp
   PRIVATE_HEADERS details/limits.hxx
   PUBLIC_LIBRARIES
      Forge::forge_raw
      values
   PRIVATE_LIBRARIES limits
)

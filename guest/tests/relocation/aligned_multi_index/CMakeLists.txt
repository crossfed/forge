cmake_minimum_required(VERSION 3.31)

project(aligned_multi_index_contract LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract_library(
   aligned_multi_index
   ID forge.tests.aligned_multi_index
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/aligned_multi_index.cppm
   SOURCES aligned_multi_index.cpp
   PUBLIC_LIBRARIES Forge::forge_contract_runtime
)

forge_add_contract(
   alignedidx
   SOURCES entry.cpp
   LIBRARIES aligned_multi_index
)

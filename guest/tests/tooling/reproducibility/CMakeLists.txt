cmake_minimum_required(VERSION 3.31)

project(reproducible_contract LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract_library(
   reproducibility_path
   ID reproducibility.path
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/reproducibility/path.cppm
   SOURCES path.cpp
)

forge_add_contract(
   hello
   SOURCES hello.cpp
   LIBRARIES reproducibility_path
)

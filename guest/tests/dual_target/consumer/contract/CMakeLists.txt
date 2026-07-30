cmake_minimum_required(VERSION 3.31)

project(ProductProtocolGuestConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)
find_package(ProductProtocol CONFIG REQUIRED)

forge_add_contract(
   consumer
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   SOURCES contract.cpp
   LIBRARIES Product::protocol
)

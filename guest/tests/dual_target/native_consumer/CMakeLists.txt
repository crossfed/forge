cmake_minimum_required(VERSION 3.31)

project(ProductProtocolInstalledConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(
   Forge CONFIG REQUIRED
   COMPONENTS chain_protocol crypto_digest raw
)

if(NOT PRODUCT_PROTOCOL_TARGETS)
   message(FATAL_ERROR "PRODUCT_PROTOCOL_TARGETS is required")
endif()
include("${PRODUCT_PROTOCOL_TARGETS}")

add_executable(product_protocol_installed_consumer main.cpp)
target_link_libraries(
   product_protocol_installed_consumer
   PRIVATE
      Product::product_chain_protocol
      Forge::forge_raw
)

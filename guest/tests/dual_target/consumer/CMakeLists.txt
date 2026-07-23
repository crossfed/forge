cmake_minimum_required(VERSION 3.31)

project(ProductProtocolConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ProductProtocol CONFIG REQUIRED)

add_executable(product_protocol_consumer host.cpp)
set_target_properties(product_protocol_consumer PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
target_link_libraries(product_protocol_consumer PRIVATE Product::protocol)

forge_add_contract(
   consumer
   LIBRARIES Product::protocol
   SOURCES contract.cpp
)

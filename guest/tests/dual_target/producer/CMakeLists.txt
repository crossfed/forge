cmake_minimum_required(VERSION 3.31)

project(ProductProtocol VERSION 1.0.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS raw chain_protocol)
find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract_library(
   product_protocol
   ID product.chain.protocol
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES
      include/product/chain/protocol.cppm
   SOURCES
      src/protocol.cpp
   PUBLIC_HEADERS
      include/product/chain/version.hpp
   PRIVATE_HEADERS
      src/limits.hpp
   LIBRARIES
      Forge::forge_raw
      Forge::forge_chain_protocol
)
set_target_properties(product_protocol PROPERTIES EXPORT_NAME protocol)
add_library(Product::protocol ALIAS product_protocol)

add_executable(product_protocol_host_tests host_tests.cpp)
set_target_properties(product_protocol_host_tests PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
target_link_libraries(
   product_protocol_host_tests
   PRIVATE
      Product::protocol
      Forge::forge_chain_protocol
)

forge_add_contract(
   product
   LIBRARIES product_protocol
   SOURCES contract.cpp
)

forge_install_contract_library(
   TARGET product_protocol
   EXPORT ProductProtocolTargets
   MODULE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/modules"
   SOURCE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/sources"
)
install(
   EXPORT ProductProtocolTargets
   FILE ProductProtocolTargets.cmake
   NAMESPACE Product::
   CXX_MODULES_DIRECTORY cxx-modules
   DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ProductProtocol"
)

configure_package_config_file(
   ProductProtocolConfig.cmake.in
   "${CMAKE_CURRENT_BINARY_DIR}/ProductProtocolConfig.cmake"
   INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ProductProtocol"
)
write_basic_package_version_file(
   "${CMAKE_CURRENT_BINARY_DIR}/ProductProtocolConfigVersion.cmake"
   VERSION "${PROJECT_VERSION}"
   COMPATIBILITY SameMajorVersion
)
install(
   FILES
      "${CMAKE_CURRENT_BINARY_DIR}/ProductProtocolConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/ProductProtocolConfigVersion.cmake"
   DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ProductProtocol"
)

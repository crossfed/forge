cmake_minimum_required(VERSION 3.31)

project(ProductProtocol VERSION 1.0.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS raw chain_protocol crypto_digest)
find_package(ForgeContract CONFIG REQUIRED)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_LINK_LIBRARIES_STRATEGY REORDER_FREELY)

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
   SOURCES src/limits.cpp
   PUBLIC_LIBRARIES Forge::forge_crypto_digest
)

forge_add_contract_library(
   protocol
   ID product.chain.protocol
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES include/product/chain/protocol.cppm
   SOURCES src/protocol.cpp
   PRIVATE_HEADERS src/limits.hpp
   PUBLIC_LIBRARIES
      Forge::forge_raw
      values
   PRIVATE_LIBRARIES limits
)

foreach(_library IN ITEMS values limits protocol)
   get_target_property(_concrete "${_library}" ALIASED_TARGET)
   get_target_property(_module_scanning "${_concrete}" CXX_SCAN_FOR_MODULES)
   get_target_property(_module_std "${_concrete}" CXX_MODULE_STD)
   get_target_property(_link_targets "${_concrete}" LINK_LIBRARIES_ONLY_TARGETS)
   get_target_property(_link_strategy "${_concrete}" LINK_LIBRARIES_STRATEGY)
   get_target_property(_optimized_dependencies "${_concrete}" OPTIMIZE_DEPENDENCIES)
   if(
      NOT _module_scanning
      OR _module_std
      OR NOT _link_targets
      OR NOT _link_strategy STREQUAL "REORDER_MINIMALLY"
      OR _optimized_dependencies
   )
      message(FATAL_ERROR "Forge Contract library did not enforce canonical module settings")
   endif()
endforeach()
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
unset(CMAKE_LINK_LIBRARIES_STRATEGY)

add_executable(product_protocol_host_tests host_tests.cpp)
set_target_properties(product_protocol_host_tests PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
target_link_libraries(
   product_protocol_host_tests
   PRIVATE
      protocol
      Forge::forge_raw
      Forge::forge_chain_protocol
)

forge_add_contract(
   product
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   SOURCES contract/product.cpp
   HEADERS contract/support/local_value.hpp
   LIBRARIES protocol
)

forge_install_contract_library(
   TARGET values
   EXPORT ProductProtocolTargets
   MODULE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/modules"
   SOURCE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/sources/values"
)
forge_install_contract_library(
   TARGET limits
   EXPORT ProductProtocolTargets
   MODULE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/modules"
   SOURCE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/sources/limits"
)
forge_install_contract_library(
   TARGET protocol
   EXPORT ProductProtocolTargets
   MODULE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/modules"
   SOURCE_DESTINATION "${CMAKE_INSTALL_DATADIR}/product-protocol/sources/protocol"
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

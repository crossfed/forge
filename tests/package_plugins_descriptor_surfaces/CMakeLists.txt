cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-descriptor-surfaces LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(
   Forge CONFIG REQUIRED COMPONENTS
      plugins_crypto_secrets
      plugins_crypto_signer
      plugins_db_store
      plugins_http_server
      plugins_p2p_node
      plugins_p2p_pubsub
      plugins_p2p_resolver
)

add_executable(forge_package_plugins_descriptor_surfaces main.cpp)
target_link_libraries(
   forge_package_plugins_descriptor_surfaces PRIVATE
      Forge::forge_plugins_crypto_secrets
      Forge::forge_plugins_crypto_signer
      Forge::forge_plugins_db_store
      Forge::forge_plugins_http_server
      Forge::forge_plugins_p2p_node
      Forge::forge_plugins_p2p_pubsub
      Forge::forge_plugins_p2p_resolver
)

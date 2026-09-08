cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-chain-signer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge 8.9 CONFIG REQUIRED COMPONENTS plugins_chain_signer)

add_executable(forge_package_plugins_chain_signer main.cpp)
target_link_libraries(
   forge_package_plugins_chain_signer PRIVATE
      Forge::forge_chain_api
      Forge::forge_plugins_chain_signer
)

cmake_minimum_required(VERSION 3.31)

project(forge-package-crypto-pki-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS crypto_pki)

add_executable(forge_package_crypto_pki_component main.cpp)
target_link_libraries(forge_package_crypto_pki_component PRIVATE Forge::forge_crypto_pki)

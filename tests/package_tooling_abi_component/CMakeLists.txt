cmake_minimum_required(VERSION 3.31)

project(forge-package-tooling-abi-component LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS tooling_abi)

get_target_property(_forge_tooling_abi_interface Forge::forge_tooling_abi INTERFACE_LINK_LIBRARIES)
if("Forge::forge_db_ids" IN_LIST _forge_tooling_abi_interface)
   message(FATAL_ERROR "Forge::forge_tooling_abi must not expose forge_db_ids as a public dependency")
endif()

add_executable(forge_package_tooling_abi_component main.cpp)
target_link_libraries(forge_package_tooling_abi_component PRIVATE Forge::forge_tooling_abi)

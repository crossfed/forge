cmake_minimum_required(VERSION 3.31)

project(forge-package-chain-savanna-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS chain_savanna)

if(NOT TARGET Threads::Threads)
   message(FATAL_ERROR "chain_savanna must provide the Threads package dependency")
endif()
get_target_property(
   forge_chain_savanna_links
   Forge::forge_chain_savanna
   INTERFACE_LINK_LIBRARIES
)
if(NOT "Threads::Threads" IN_LIST forge_chain_savanna_links)
   message(FATAL_ERROR "chain_savanna must propagate Threads::Threads")
endif()

add_executable(forge_package_chain_savanna_component main.cpp)
target_link_libraries(forge_package_chain_savanna_component PRIVATE Forge::forge_chain_savanna)

cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-db-objectdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS plugins_db_objectdb)

add_executable(forge_package_plugins_db_objectdb main.cpp)
target_link_libraries(forge_package_plugins_db_objectdb PRIVATE Forge::forge_plugins_db_objectdb)

cmake_minimum_required(VERSION 3.31)

project(forge-package-auth-clients LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge 8.9 CONFIG REQUIRED COMPONENTS auth_oauth2 auth_workload auth_appauth)

add_executable(forge_package_auth_clients main.cpp)
target_link_libraries(
   forge_package_auth_clients PRIVATE
      Forge::forge_auth_oauth2
      Forge::forge_auth_workload
      Forge::forge_auth_appauth
)

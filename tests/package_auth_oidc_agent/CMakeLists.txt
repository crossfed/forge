cmake_minimum_required(VERSION 3.31)

project(forge-package-auth-oidc-agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge 8.9 CONFIG REQUIRED COMPONENTS auth_oidc_agent)

add_executable(forge_package_auth_oidc_agent main.cpp)
target_link_libraries(
   forge_package_auth_oidc_agent PRIVATE
      Forge::forge_auth_oidc_agent
)

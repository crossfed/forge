cmake_minimum_required(VERSION 3.31)

project(hello_contract LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract(
   hello
   SOURCES hello.cpp
)

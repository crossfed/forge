forge_add_contract(
   consumer
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.."
   SOURCES contract.cpp
   LIBRARIES Product::protocol
)

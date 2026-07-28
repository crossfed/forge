add_library(
   forge_contract_graph STATIC
   graph.cpp
)
forge_target_modules_at(forge_contract_graph contract/graph)

target_include_directories(forge_contract_graph PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_contract_graph
   PUBLIC
      forge_codec_json
)

add_library(
   forge_chain_quorum STATIC
   evaluate.cpp
)
forge_target_modules_at(forge_chain_quorum chain/quorum)

target_include_directories(forge_chain_quorum PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain_quorum PUBLIC
      Boost::headers
      forge_exceptions
)

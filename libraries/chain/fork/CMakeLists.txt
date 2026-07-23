add_library(
   forge_chain_fork STATIC
   types.cpp
)
forge_target_modules_at(forge_chain_fork chain/fork)

target_include_directories(forge_chain_fork PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain_fork PUBLIC
      Boost::headers
      forge_exceptions
      Threads::Threads
)

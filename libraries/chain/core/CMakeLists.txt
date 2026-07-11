add_library(
   forge_chain_core STATIC
   merkle.cpp
)
forge_target_modules_at(forge_chain_core chain/core)

target_include_directories(forge_chain_core PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain_core PUBLIC
      forge_crypto
      forge_exceptions
      forge_raw
)

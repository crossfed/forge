add_library(
   forge_api_auth STATIC
   authenticated_caller.cpp
)
forge_target_modules_at(forge_api_auth api/auth)

target_include_directories(forge_api_auth PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_api_auth PUBLIC
      Boost::headers
      forge_api_core
      forge_crypto_digest
      forge_raw
)

add_library(
   forge_auth_workload STATIC
   projected_token_file.cpp
   subject_token.cpp
   subject_token_source.cpp
)
forge_target_modules_at(forge_auth_workload auth/workload)

target_include_directories(forge_auth_workload PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_auth_workload PUBLIC
      Boost::headers
      forge_asio
      forge_crypto_core
      forge_exceptions
)

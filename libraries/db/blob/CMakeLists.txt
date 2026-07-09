add_library(
   forge_db_blob STATIC
   store.cpp
   transaction.cpp
)
forge_target_modules_at(forge_db_blob db/blob)

target_include_directories(forge_db_blob PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_db_blob PUBLIC
      Boost::asio
      forge_crypto
      forge_db_core
      forge_exceptions
      forge_raw
      forge_variant
)

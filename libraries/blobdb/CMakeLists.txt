add_library(
   forge_blobdb STATIC
   store.cpp
   transaction.cpp
)
forge_target_modules(forge_blobdb blobdb)

target_include_directories(forge_blobdb PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_blobdb PUBLIC
      Boost::asio
      forge_db
      forge_exceptions
)

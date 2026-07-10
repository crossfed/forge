add_library(
   forge_db_object STATIC
   snapshot.cpp
   store.cpp
   store_impl.cpp
   transaction.cpp
   write_gate.cpp
)
forge_target_modules_at(forge_db_object db/object)

target_include_directories(forge_db_object PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_db_object PUBLIC
      Boost::asio
      forge_db_core
      forge_exceptions
      forge_ids
      forge_raw
)

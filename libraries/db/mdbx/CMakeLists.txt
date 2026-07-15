add_library(
   forge_db_mdbx STATIC
   driver.cpp
   driver_impl.cpp
   environment.cpp
   transaction_session.cpp
   snapshot_session.cpp
   error.cpp
   scan.cpp
)
forge_target_modules_at(forge_db_mdbx db/mdbx)

target_include_directories(forge_db_mdbx PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_include_directories(
   forge_db_mdbx PRIVATE
      "${CMAKE_SOURCE_DIR}/vendor/libmdbx"
)

target_link_libraries(
   forge_db_mdbx PUBLIC
      forge_asio
      forge_db_core
   PRIVATE
      mdbx-internal
)

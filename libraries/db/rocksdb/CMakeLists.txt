add_library(
   forge_db_rocksdb STATIC
   driver.cpp
)
forge_target_modules_at(forge_db_rocksdb db/rocksdb)

target_include_directories(forge_db_rocksdb PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_db_rocksdb PUBLIC
      forge_db_core
      forge_rocksdb
)

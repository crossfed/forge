add_library(forge_db_ids STATIC object_id.cpp)
forge_target_modules_at(forge_db_ids db/ids)

target_include_directories(forge_db_ids PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_db_ids PUBLIC
      forge_raw
      forge_variant
)

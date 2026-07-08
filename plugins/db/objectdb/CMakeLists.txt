add_library(
   forge_plugins_db_objectdb STATIC
   plugin.cpp
   config.cpp
   plugin_impl.cpp
   api_handle_impl.cpp
   api.cpp
)
forge_target_modules_at(forge_plugins_db_objectdb plugins/db/objectdb)

target_include_directories(
   forge_plugins_db_objectdb
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_DB_OBJECTDB_INCLUDE_DIR}>"
)
target_include_directories(forge_plugins_db_objectdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
   forge_plugins_db_objectdb PUBLIC
      forge_app
      forge_api_core
      forge_config
      forge_schema
      forge_db
      forge_objectdb
      forge_exceptions
      Boost::asio
)

target_compile_definitions(
   forge_plugins_db_objectdb PRIVATE
      FORGE_PLUGINS_DB_OBJECTDB_HAS_ROCKSDB=$<BOOL:${FORGE_HAS_ROCKSDB}>
)

if(TARGET forge_db_rocksdb)
   target_link_libraries(forge_plugins_db_objectdb PRIVATE forge_db_rocksdb)
endif()

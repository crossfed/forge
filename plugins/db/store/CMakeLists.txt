add_library(
   forge_plugins_db_store STATIC
   plugin.cpp
   config.cpp
   plugin_impl.cpp
   api_handle_impl.cpp
   api.cpp
)
forge_target_modules_at(forge_plugins_db_store plugins/db/store)

target_include_directories(
   forge_plugins_db_store
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_DB_STORE_INCLUDE_DIR}>"
)
target_include_directories(forge_plugins_db_store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
   forge_plugins_db_store PUBLIC
      forge_app
      forge_api
      forge_config
      forge_schema
      forge_db_core
      forge_db_object
      forge_db_blob
      forge_exceptions
      Boost::asio
)

target_compile_definitions(
   forge_plugins_db_store PRIVATE
      FORGE_PLUGINS_DB_STORE_HAS_ROCKSDB=$<BOOL:${FORGE_HAS_ROCKSDB}>
)

if(TARGET forge_db_rocksdb)
   target_link_libraries(forge_plugins_db_store PRIVATE forge_db_rocksdb)
endif()

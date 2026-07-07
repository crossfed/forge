add_library(
   forge_plugins_db_object STATIC
   plugin.cpp
   config.cpp
   plugin_impl.cpp
   api_handle_impl.cpp
   api.cpp
)
forge_target_modules_at(forge_plugins_db_object plugins/db/object)

target_include_directories(
   forge_plugins_db_object
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_DB_OBJECT_INCLUDE_DIR}>"
)
target_include_directories(forge_plugins_db_object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
   forge_plugins_db_object PUBLIC
      forge_app
      forge_api
      forge_config
      forge_schema
      forge_db
      forge_db_object
      forge_exceptions
      Boost::asio
)

target_compile_definitions(
   forge_plugins_db_object PRIVATE
      FORGE_PLUGINS_DB_OBJECT_HAS_ROCKSDB=$<BOOL:${FORGE_HAS_ROCKSDB}>
)

if(TARGET forge_db_rocksdb)
   target_link_libraries(forge_plugins_db_object PRIVATE forge_db_rocksdb)
endif()

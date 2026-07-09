add_library(forge_config_env STATIC env.cpp)
forge_target_modules_at(forge_config_env config/env)

target_include_directories(forge_config_env PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_config_env PUBLIC
      forge_config_core
      forge_schema
)

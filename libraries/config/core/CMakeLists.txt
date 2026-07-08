add_library(
   forge_config_core STATIC
   key_path.cpp
   document.cpp
   component.cpp
   decode.cpp
   migration.cpp
)
forge_target_modules_at(forge_config_core config/core)

target_include_directories(forge_config_core PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_config_core PUBLIC
      forge_schema
)

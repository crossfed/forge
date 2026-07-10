add_library(forge_config_program_options STATIC program_options.cpp)
forge_target_modules_at(forge_config_program_options config/program_options)

target_include_directories(forge_config_program_options PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_config_program_options PUBLIC
      forge_config_core
      forge_schema
      Boost::program_options
)

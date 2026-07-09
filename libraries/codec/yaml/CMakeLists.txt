add_library(forge_codec_yaml STATIC yaml.cpp)
forge_target_modules_at(forge_codec_yaml codec/yaml)

target_include_directories(forge_codec_yaml PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_codec_yaml PUBLIC
      forge_config_core
      forge_schema
      forge_variant
   PRIVATE
      glaze::glaze
)

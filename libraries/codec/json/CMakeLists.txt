add_library(forge_codec_json STATIC exact.cpp json.cpp)
forge_target_modules_at(forge_codec_json codec/json)

target_include_directories(forge_codec_json PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_codec_json PUBLIC
      forge_core
      forge_config_core
      forge_schema
      forge_variant
   PRIVATE
      glaze::glaze
)
target_compile_definitions(
   forge_codec_json
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)

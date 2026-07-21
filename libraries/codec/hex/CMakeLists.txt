add_library(forge_codec_hex STATIC hex.cpp)
forge_target_modules_at(forge_codec_hex codec/hex)

target_include_directories(forge_codec_hex PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(forge_codec_hex PUBLIC forge_exceptions)

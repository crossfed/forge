add_library(forge_crypto_bn256 STATIC bn256.cpp)
forge_target_modules_at(forge_crypto_bn256 crypto/bn256)

target_include_directories(forge_crypto_bn256 PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(forge_crypto_bn256 PRIVATE bn256)

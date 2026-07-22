add_library(
   forge_crypto_symmetric STATIC
   aes.cpp
   chacha20_poly1305.cpp
   kdf.cpp
)
forge_target_modules_at(forge_crypto_symmetric crypto/symmetric)

target_include_directories(forge_crypto_symmetric PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(
   forge_crypto_symmetric
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
target_link_libraries(
   forge_crypto_symmetric
   PUBLIC
      forge_crypto_core
      forge_exceptions
   PRIVATE OpenSSL::Crypto
)

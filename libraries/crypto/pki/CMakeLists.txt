add_library(
   forge_crypto_pki STATIC
   der.cpp
   pem.cpp
   x509.cpp
)
forge_target_modules_at(forge_crypto_pki crypto/pki)

target_include_directories(forge_crypto_pki PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(
   forge_crypto_pki
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
target_link_libraries(
   forge_crypto_pki
   PUBLIC
      forge_codec_hex
      forge_crypto_asymmetric
      forge_crypto_core
      forge_crypto_digest
      forge_exceptions
   PRIVATE OpenSSL::Crypto
)

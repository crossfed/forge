add_library(
   forge_crypto_math STATIC
   bigint.cpp
   modular_arithmetic.cpp
)
forge_target_modules_at(forge_crypto_math crypto/math)

target_include_directories(forge_crypto_math PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(
   forge_crypto_math
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
target_link_libraries(
   forge_crypto_math
   PUBLIC
      forge_codec_base64
      forge_core
      forge_crypto_core
      forge_exceptions
      forge_variant
      GMP::gmp
      OpenSSL::Crypto
)

add_library(
   forge_crypto_digest STATIC
   blake2.cpp
   ripemd160.cpp
   sha1.cpp
   sha224.cpp
   sha256.cpp
   sha3.cpp
   sha512.cpp
)
forge_target_modules_at(forge_crypto_digest crypto/digest)

target_include_directories(forge_crypto_digest PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(
   forge_crypto_digest
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
target_link_libraries(
   forge_crypto_digest
   PUBLIC
      forge_codec_hex
      forge_core
      forge_crypto_core
      forge_exceptions
      forge_raw
      forge_variant
      Boost::headers
   PRIVATE OpenSSL::Crypto
)
forge_target_contract_guest_component(forge_crypto_digest ID forge.crypto.digest)

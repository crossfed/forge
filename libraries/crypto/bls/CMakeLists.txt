add_library(forge_crypto_bls_values STATIC)
target_sources(
   forge_crypto_bls_values
   PUBLIC
      FILE_SET CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
      FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/forge/crypto/bls/bls_values.cppm"
)
target_compile_features(forge_crypto_bls_values PUBLIC cxx_std_23)
target_include_directories(forge_crypto_bls_values PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(forge_crypto_bls_values PUBLIC forge_raw)
forge_target_contract_guest_component(forge_crypto_bls_values ID forge.crypto.bls_values)

add_library(
   forge_crypto_bls STATIC
   bls.cpp
   bls_primitives.cpp
   bls_private_key.cpp
   bls_serialization.cpp
)
set(FORGE_crypto_bls_DISABLED_MODULES bls_values)
forge_target_modules_at(forge_crypto_bls crypto/bls)
unset(FORGE_crypto_bls_DISABLED_MODULES)

target_include_directories(forge_crypto_bls PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(
   forge_crypto_bls
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
target_link_libraries(
   forge_crypto_bls
   PUBLIC
      forge_codec_base64
      forge_core
      forge_crypto_core
      forge_crypto_digest
      forge_crypto_bls_values
      forge_exceptions
      forge_raw
      forge_variant
   PRIVATE
      bls12-381
      OpenSSL::Crypto
)

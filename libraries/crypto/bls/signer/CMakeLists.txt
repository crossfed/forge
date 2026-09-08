add_library(
   forge_crypto_bls_signer STATIC
   configured_provider.cpp
   configured_provider_impl.cpp
   provider.cpp
)
forge_target_modules_at(forge_crypto_bls_signer crypto/bls/signer)

target_include_directories(forge_crypto_bls_signer PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_crypto_bls_signer PUBLIC
      Boost::headers
      forge_crypto_bls
      forge_exceptions
)

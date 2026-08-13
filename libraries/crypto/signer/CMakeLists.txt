add_library(
   forge_crypto_signer STATIC
   configured_provider.cpp
   configured_provider_impl.cpp
   provider.cpp
)
forge_target_modules_at(forge_crypto_signer crypto/signer)

target_include_directories(forge_crypto_signer PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_crypto_signer PUBLIC
      Boost::headers
      forge_crypto_asymmetric
      forge_crypto_core
      forge_exceptions
)

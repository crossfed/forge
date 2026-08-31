add_library(
   forge_plugins_crypto_signer STATIC
      descriptor.cpp
      plugin.cpp
      api_impl.cpp
      bls_api_impl.cpp
      config.cpp
      plugin_impl.cpp
      details/loaded_bls_key.hxx
      details/loaded_key.hxx
)
forge_target_modules_at(forge_plugins_crypto_signer plugins/crypto/signer)

target_include_directories(
   forge_plugins_crypto_signer
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_CRYPTO_SIGNER_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_crypto_signer PUBLIC
      forge_app
      forge_api_core
      forge_config_core
      forge_schema
      forge_crypto_asymmetric
      forge_crypto_bls
      forge_crypto_digest
      forge_exceptions
      Boost::asio
)

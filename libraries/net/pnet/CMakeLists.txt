add_library(
   forge_net_pnet STATIC
   protector.cpp
   protected_stream.cpp
   details/protected_stream.hxx
)
forge_target_modules_at(forge_net_pnet net/pnet)

target_include_directories(forge_net_pnet PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_pnet PUBLIC
      forge_exceptions
      forge_codec_hex
      forge_crypto_core
      forge_crypto_digest
      forge_crypto_symmetric
      forge_asio
      forge_net_transport
      Boost::asio
)

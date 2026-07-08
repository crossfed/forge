add_library(
   forge_net_quic STATIC
   endpoint.cpp
   security.cpp
   options.cpp
   libp2p.cpp
   framed_stream.cpp
   runtime.cpp
   quic_engine.cpp
   stream.cpp
   connection.cpp
   transport.cpp
   connector.cpp
   listener.cpp
)
forge_target_modules_at(forge_net_quic net/quic)

target_include_directories(forge_net_quic PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_quic PUBLIC
      forge_exceptions
      forge_api_core
      forge_api_transport
      forge_raw
      forge_crypto
      forge_asio
      forge_net_transport
      Boost::asio
      OpenSSL::SSL
      OpenSSL::Crypto
      Ngtcp2::ngtcp2
      Ngtcp2::crypto_ossl
)

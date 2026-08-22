add_library(
   forge_net_tls STATIC
   context.cpp
)
forge_target_modules_at(forge_net_tls net/tls)

target_include_directories(forge_net_tls PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_tls PUBLIC
      forge_exceptions
      Boost::asio
      Boost::beast
      OpenSSL::SSL
   PRIVATE
      forge_crypto_pki
)

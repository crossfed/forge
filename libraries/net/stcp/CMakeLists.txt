add_library(
   forge_net_stcp STATIC
   connection.cpp
   connector.cpp
   listener.cpp
)
forge_target_modules_at(forge_net_stcp net/stcp)

target_include_directories(forge_net_stcp PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_stcp PUBLIC
      forge_exceptions
      forge_net_transport
      forge_net_tcp
      forge_crypto
      Boost::asio
      OpenSSL::SSL
)

add_library(
   forge_net_websocket STATIC
   connection.cpp
   client.cpp
)
forge_target_modules_at(forge_net_websocket net/websocket)

target_include_directories(forge_net_websocket PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_websocket PUBLIC
      forge_exceptions
      forge_asio
      Boost::asio
      Boost::beast
      OpenSSL::SSL
)

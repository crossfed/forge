add_library(
   forge_api_transport STATIC
   client.cpp
   connection.cpp
   server.cpp
)
forge_target_modules_at(forge_api_transport api/transport)

target_include_directories(forge_api_transport PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_api_transport PUBLIC
      forge_exceptions
      forge_api_core
      forge_api_stream
      forge_raw
      forge_net_transport
      Boost::asio
)

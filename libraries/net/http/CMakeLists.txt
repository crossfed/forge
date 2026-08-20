add_library(
   forge_net_http STATIC
   types.cpp
   base_url.cpp
   target.cpp
   range.cpp
   upload.cpp
   route_context.cpp
   middleware.cpp
   connection.cpp
   client.cpp
   stream.cpp
   router.cpp
   router_server_access.cpp
   stream_server_access.cpp
   file.cpp
   server.cpp
)
forge_target_modules_at(forge_net_http net/http)

target_include_directories(forge_net_http PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_http PUBLIC
      forge_exceptions
      forge_api_core
      forge_asio
      forge_codec_json
      forge_schema
      forge_net_tls
      forge_net_websocket
      Boost::asio
      Boost::beast
      Boost::url
)

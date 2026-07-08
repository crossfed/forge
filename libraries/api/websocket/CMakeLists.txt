add_library(forge_api_websocket STATIC)
forge_target_modules_at(forge_api_websocket api/websocket)

target_include_directories(forge_api_websocket PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_api_websocket PUBLIC
      forge_exceptions
      forge_api_core
      forge_raw
      forge_net_websocket
      Boost::asio
)

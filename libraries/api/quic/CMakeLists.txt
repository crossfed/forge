add_library(forge_api_quic STATIC)
forge_target_modules_at(forge_api_quic api/quic)

target_include_directories(forge_api_quic PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_api_quic PUBLIC
      forge_api_core
      forge_api_transport
      forge_net_quic
      forge_net_transport
      Boost::asio
)

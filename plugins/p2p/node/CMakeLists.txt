add_library(
   forge_plugins_p2p_node STATIC
      plugin.cpp
      node_api.cpp
      diagnostics_source.cpp
      pubsub_source.cpp
      config.cpp
      plugin_impl.cpp
)
forge_target_modules_at(forge_plugins_p2p_node plugins/p2p/node)

target_include_directories(
   forge_plugins_p2p_node
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_P2P_NODE_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_p2p_node PUBLIC
      forge_app
      forge_api_core
      forge_api_transport
      forge_api_p2p
      forge_net_p2p
      forge_config_core
      forge_schema
      forge_asio
      forge_exceptions
      Boost::asio
)

add_library(
   forge_net_yamux STATIC
   session.cpp
)
forge_target_modules_at(forge_net_yamux net/yamux)

target_include_directories(forge_net_yamux PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_yamux PUBLIC
      forge_exceptions
      forge_net_transport
      Boost::asio
   PRIVATE
      forge_asio
)

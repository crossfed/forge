add_library(
   forge_net_dns STATIC
   resolver.cpp
   cares_library.cpp
   nameserver_formatter.cpp
   resolver_socket_registry.cpp
   resolver_socket_watch.cpp
   resolver_query.cpp
   resolver_impl.cpp
   details/cares_library.hxx
   details/nameserver_formatter.hxx
   details/resolver_socket_registry.hxx
   details/resolver_socket_watch.hxx
   details/resolver_query.hxx
   details/resolver_impl.hxx
)
forge_target_modules_at(forge_net_dns net/dns)

target_include_directories(
   forge_net_dns
   PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS}
   PRIVATE
      "${PROJECT_SOURCE_DIR}/vendor/c-ares"
      "${PROJECT_SOURCE_DIR}/vendor/c-ares/include"
      "${CMAKE_BINARY_DIR}/vendor/c-ares"
)

target_link_libraries(
   forge_net_dns
   PUBLIC
      forge_asio
      forge_exceptions
      Boost::asio
   PRIVATE
      c-ares
)

if(WIN32)
   target_compile_definitions(forge_net_dns PRIVATE CARES_STATICLIB)
endif()

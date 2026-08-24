add_library(
   forge_api_p2p STATIC
      publication.cpp
      publication_impl.cpp
      details/publication_impl.hxx
)
forge_target_modules_at(forge_api_p2p api/p2p)

target_include_directories(forge_api_p2p PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_api_p2p PUBLIC
      forge_exceptions
      forge_api_core
      forge_api_stream
      forge_net_p2p
      Boost::asio
)

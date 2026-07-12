add_library(
   forge_db_object STATIC
   details/ordered_key.hxx
   details/record_key.hxx
   snapshot.cpp
   store.cpp
   store_impl.cpp
   transaction.cpp
   write_gate.cpp
)
forge_target_modules_at(forge_db_object db/object)

target_include_directories(forge_db_object PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_include_directories(forge_db_object PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/details>")
target_include_directories(
   forge_db_object INTERFACE
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_LIBDIR}/cmake/Forge/details/db_object>"
)

install(
   FILES details/ordered_key.hxx details/record_key.hxx
   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Forge/details/db_object
   COMPONENT dev
   EXCLUDE_FROM_ALL
)

target_link_libraries(
   forge_db_object PUBLIC
      Boost::asio
      forge_db_core
      forge_exceptions
      forge_ids
      forge_raw
)

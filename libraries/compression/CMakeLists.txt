add_library(
   forge_compression STATIC
   zlib.cpp
)
forge_target_modules(forge_compression compression)

target_include_directories(forge_compression PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_compression PUBLIC
      Boost::iostreams
      ZLIB::ZLIB
      forge_exceptions
)

add_library(
   forge_raw STATIC
   codec.cpp
   stream.cpp
   varint.cpp
)
forge_target_modules(forge_raw raw)
target_include_directories(forge_raw PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_raw PUBLIC
      forge_core
      forge_exceptions
      forge_reflect
      forge_variant
      Boost::headers
      Boost::multiprecision
)
forge_target_contract_guest_component(forge_raw ID forge.raw)

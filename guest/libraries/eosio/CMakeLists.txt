add_library(forge_guest_eosio INTERFACE)
target_include_directories(
   forge_guest_eosio
   INTERFACE
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(forge_guest_eosio INTERFACE forge_guest_contract)
target_compile_features(forge_guest_eosio INTERFACE cxx_std_23)

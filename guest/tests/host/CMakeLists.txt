add_library(
   forge_contract_test_host STATIC
   host.cpp
   host_impl.cpp
   memory_driver.cpp
   softfloat.cpp
)

target_sources(
   forge_contract_test_host
   PUBLIC
      FILE_SET forge_contract_test_host_modules TYPE CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
      FILES
         include/forge/contract/testing/exceptions.cppm
         include/forge/contract/testing/schema.cppm
         include/forge/contract/testing/state.cppm
         include/forge/contract/testing/host.cppm
)

target_include_directories(
   forge_contract_test_host
   PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/contract/include"
)

target_link_libraries(
   forge_contract_test_host
   PUBLIC
      Forge::forge_asio
      Forge::forge_db_core
      Forge::forge_db_object
      Forge::forge_exceptions
      Forge::forge_crypto
      Forge::forge_vm_wasm
      Forge::forge_chain_protocol
   PRIVATE
      Forge::vm_wasm_softfloat_internal
      Boost::headers
)

target_compile_features(forge_contract_test_host PUBLIC cxx_std_23)

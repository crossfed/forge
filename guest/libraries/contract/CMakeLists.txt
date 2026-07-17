add_library(forge_guest_contract STATIC dispatcher.cpp)
target_sources(
   forge_guest_contract
   PUBLIC
      FILE_SET CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
      FILES
         include/forge/contract/intrinsics.cppm
         include/forge/contract/contract.cppm
         include/forge/contract/dispatcher.cppm
         include/forge/contract/multi_index.cppm
         include/forge/contract/singleton.cppm
)
target_include_directories(
   forge_guest_contract
   PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
   forge_guest_contract
   PUBLIC
      forge_guest_runtime
      forge_guest_raw
      forge_guest_chain_protocol
)
target_compile_features(forge_guest_contract PUBLIC cxx_std_23)

cmake_minimum_required(VERSION 3.31)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
   project(ForgeContractTools LANGUAGES C CXX)

   include(GNUInstallDirs)

   set(CMAKE_CXX_STANDARD 23)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   set(CMAKE_CXX_EXTENSIONS OFF)

   find_package(
      Forge CONFIG REQUIRED
      COMPONENTS
         tooling_abi
         tooling_attributes
         tooling_validation
         tooling_manifest
   )
endif()

add_executable(abigen abigen/main.cpp)
target_link_libraries(abigen PRIVATE Forge::forge_tooling_abi)

add_library(attr_plugin MODULE attr-plugin/plugin.cpp)
target_link_libraries(
   attr_plugin
   PRIVATE
      Forge::forge_tooling_abi
      Forge::forge_tooling_attributes
      clang-cpp
      LLVM
)
target_include_directories(attr_plugin SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_compile_definitions(attr_plugin PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(attr_plugin PROPERTIES PREFIX "" OUTPUT_NAME "attr-plugin")

add_executable(contract_check contract-check/main.cpp)
target_link_libraries(contract_check PRIVATE Forge::forge_tooling_validation)
set_target_properties(contract_check PROPERTIES OUTPUT_NAME "contract-check")

add_executable(contract_manifest contract-manifest/main.cpp)
target_link_libraries(contract_manifest PRIVATE Forge::forge_tooling_manifest)
set_target_properties(contract_manifest PROPERTIES OUTPUT_NAME "contract-manifest")

foreach(_target abigen attr_plugin contract_check contract_manifest)
   target_compile_options(
      ${_target}
      PRIVATE
         "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
         "-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
         "-fdebug-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=."
   )
endforeach()

if(APPLE)
   set(_forge_contract_executable_rpath "@loader_path/../lib")
   set(_forge_contract_plugin_rpath "@loader_path/..")
elseif(UNIX)
   set(_forge_contract_executable_rpath "\$ORIGIN/../lib")
   set(_forge_contract_plugin_rpath "\$ORIGIN/..")
endif()
if(DEFINED _forge_contract_executable_rpath)
   set_target_properties(
      abigen contract_check contract_manifest
      PROPERTIES
         BUILD_WITH_INSTALL_RPATH ON
         INSTALL_RPATH "${_forge_contract_executable_rpath}"
   )
   set_target_properties(
      attr_plugin
      PROPERTIES
         BUILD_WITH_INSTALL_RPATH ON
         INSTALL_RPATH "${_forge_contract_plugin_rpath}"
   )
endif()

install(TARGETS abigen contract_check contract_manifest RUNTIME DESTINATION bin)
install(TARGETS attr_plugin LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/forge-contract")

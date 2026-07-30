cmake_minimum_required(VERSION 3.31)

project(MultiConfigGuestContract LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract(
   configuration
   SOURCES entry.cpp
)

add_library(
   configuration_compile_probe
   OBJECT
      release_probe.cpp
)

set(_artifact_directory "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set(_configuration_marker "${_artifact_directory}/built-$<CONFIG>.txt")
add_custom_command(
   OUTPUT "${_configuration_marker}"
   COMMAND
      "${CMAKE_COMMAND}"
      "-DOUTPUT=${_configuration_marker}"
      "-DCONFIGURATION=$<CONFIG>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/write_configuration.cmake"
   DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/write_configuration.cmake"
   VERBATIM
)
add_custom_target(
   configuration_marker
   DEPENDS "${_configuration_marker}"
)
add_dependencies(
   configuration_artifacts
   configuration_compile_probe
   configuration_marker
)

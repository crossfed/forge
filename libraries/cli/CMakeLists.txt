add_library(
   forge_cli STATIC
   command.cpp
   completion.cpp
   parser.cpp
   runner.cpp
   terminal.cpp
)
forge_target_modules(forge_cli cli)

file(READ "${PROJECT_SOURCE_DIR}/vendor/CLI11/include/CLI/Version.hpp" _forge_cli11_version_header)
if(NOT _forge_cli11_version_header MATCHES "#define CLI11_VERSION \"2\\.6\\.2\"")
   message(FATAL_ERROR "forge_cli requires the vendored CLI11 v2.6.2 header set")
endif()

target_include_directories(forge_cli PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_include_directories(
   forge_cli SYSTEM PRIVATE
      "${PROJECT_SOURCE_DIR}/vendor/CLI11/include"
)

target_link_libraries(
   forge_cli
   PUBLIC
      Boost::headers
      forge_asio
      forge_core
      forge_exceptions
   PRIVATE
      Boost::charconv
)

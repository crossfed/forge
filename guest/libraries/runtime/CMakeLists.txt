cmake_minimum_required(VERSION 3.31)

project(ForgeGuestRuntime LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT FORGE_GUEST_INCLUDE_DIR OR NOT IS_DIRECTORY "${FORGE_GUEST_INCLUDE_DIR}")
   message(FATAL_ERROR "FORGE_GUEST_INCLUDE_DIR must contain generated contract headers")
endif()

add_library(
   forge_guest_runtime STATIC
   allocator.cpp
   allocator_new_delete.cpp
   errno.cpp
   memory.cpp
)

target_include_directories(
   forge_guest_runtime
   PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${FORGE_GUEST_INCLUDE_DIR}"
)
target_compile_features(forge_guest_runtime PUBLIC cxx_std_23)
target_compile_definitions(forge_guest_runtime PRIVATE FORGE_CONTRACT_GUEST=1)
target_compile_options(
   forge_guest_runtime
   PRIVATE
      -fno-exceptions
      -fno-rtti
      -fno-threadsafe-statics
      -ffreestanding
      -fvisibility=hidden
      -fno-ident
      -mcpu=mvp
)

install(TARGETS forge_guest_runtime ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

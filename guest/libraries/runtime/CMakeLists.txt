cmake_minimum_required(VERSION 3.31)

project(ForgeGuestRuntime LANGUAGES C CXX)

include(GNUInstallDirs)

if(NOT FORGE_GUEST_INCLUDE_DIR OR NOT IS_DIRECTORY "${FORGE_GUEST_INCLUDE_DIR}")
   message(FATAL_ERROR "FORGE_GUEST_INCLUDE_DIR must contain generated contract headers")
endif()
if(NOT FORGE_GUEST_LIBC_SOURCE_DIR OR NOT IS_DIRECTORY "${FORGE_GUEST_LIBC_SOURCE_DIR}/src/math")
   message(FATAL_ERROR "FORGE_GUEST_LIBC_SOURCE_DIR must contain the pinned CDT musl sources")
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

file(GLOB _forge_guest_math_sources CONFIGURE_DEPENDS "${FORGE_GUEST_LIBC_SOURCE_DIR}/src/math/*.c")
add_library(forge_guest_math STATIC ${_forge_guest_math_sources})
set_target_properties(forge_guest_math PROPERTIES OUTPUT_NAME m)
target_compile_features(forge_guest_math PRIVATE c_std_11)
target_include_directories(
   forge_guest_math
   PRIVATE
      "${FORGE_GUEST_LIBC_SOURCE_DIR}/src/internal"
      "${FORGE_GUEST_LIBC_SOURCE_DIR}/arch/eos"
)
target_compile_options(
   forge_guest_math
   PRIVATE
      -idirafter
      "${FORGE_GUEST_LIBC_SOURCE_DIR}/include"
      -ffreestanding
      -fvisibility=hidden
      -fno-ident
      -mcpu=mvp
)

install(TARGETS forge_guest_math ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(
   forge_guest_runtime STATIC
   allocator.cpp
   allocator_new_delete.cpp
   memory.cpp
)

target_include_directories(
   forge_guest_runtime
   PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${FORGE_GUEST_SYSROOT_INCLUDE_DIR}"
      "${FORGE_GUEST_INCLUDE_DIR}"
)
target_compile_features(forge_guest_runtime PUBLIC cxx_std_23)

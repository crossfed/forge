add_library(
   forge_vm_wasm STATIC
   allocator.cpp
   debug_info.cpp
   signals.cpp
   types.cpp
   watchdog.cpp
)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
   list(APPEND FORGE_vm_wasm_DISABLED_MODULES x86_64)
endif()

forge_target_modules_at(forge_vm_wasm vm/wasm)

target_include_directories(
   forge_vm_wasm
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
)

target_link_libraries(
   forge_vm_wasm
   PUBLIC
      forge_exceptions
      Threads::Threads
      softfloat
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
   target_compile_definitions(forge_vm_wasm PUBLIC FORGE_VM_WASM_HAS_JIT=1)
else()
   target_compile_definitions(forge_vm_wasm PUBLIC FORGE_VM_WASM_HAS_JIT=0)
endif()

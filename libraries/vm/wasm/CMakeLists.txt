add_library(
   forge_vm_wasm STATIC
   watchdog.cpp
)
forge_target_modules_at(forge_vm_wasm vm/wasm)

target_include_directories(
   forge_vm_wasm
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_LIBDIR}/forge/internal/vm_wasm>"
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

target_compile_options(
   forge_vm_wasm
   PRIVATE
      -Wno-unused-parameter
      -Wno-unused-variable
      -Wno-include-angled-in-module-purview
)

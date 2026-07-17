if(NOT DEFINED FORGE_CONTRACT_TOOLCHAIN_SELECTOR OR NOT EXISTS "${FORGE_CONTRACT_TOOLCHAIN_SELECTOR}")
   message(FATAL_ERROR "FORGE_CONTRACT_TOOLCHAIN_SELECTOR must name the developer toolchain selector")
endif()
if(NOT DEFINED FORGE_CONTRACT_TEST_ROOT OR FORGE_CONTRACT_TEST_ROOT STREQUAL "")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_ROOT is required")
endif()

set(_root "${FORGE_CONTRACT_TEST_ROOT}")
set(_selected "${_root}/selected/bin")
set(_path_toolchain "${_root}/path/bin")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_selected}" "${_path_toolchain}")

foreach(_tool clang++ clang-scan-deps wasm-ld llvm-ar llvm-ranlib)
   file(WRITE "${_selected}/${_tool}" "#!/bin/sh\nexit 0\n")
   file(WRITE "${_path_toolchain}/${_tool}" "#!/bin/sh\nexit 0\n")
   file(
      CHMOD
      "${_selected}/${_tool}"
      "${_path_toolchain}/${_tool}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
   )
endforeach()

set(ENV{PATH} "${_path_toolchain}:$ENV{PATH}")
set(LLVM_TOOLS_BINARY_DIR "${_selected}")
set(FORGE_CONTRACT_CLANG "")
set(FORGE_CONTRACT_WASM_LD "")
set(FORGE_CONTRACT_LLVM_AR "")
set(FORGE_CONTRACT_LLVM_RANLIB "")

include("${FORGE_CONTRACT_TOOLCHAIN_SELECTOR}")
forge_contract_select_developer_toolchain()

foreach(
   _binding
   "FORGE_CONTRACT_CLANG;clang++"
   "FORGE_CONTRACT_CLANG_SCAN_DEPS;clang-scan-deps"
   "FORGE_CONTRACT_WASM_LD;wasm-ld"
   "FORGE_CONTRACT_LLVM_AR;llvm-ar"
   "FORGE_CONTRACT_LLVM_RANLIB;llvm-ranlib"
)
   list(GET _binding 0 _variable)
   list(GET _binding 1 _program)
   if(NOT "${${_variable}}" STREQUAL "${_selected}/${_program}")
      message(FATAL_ERROR "${_variable} escaped the selected LLVM package: ${${_variable}}")
   endif()
endforeach()

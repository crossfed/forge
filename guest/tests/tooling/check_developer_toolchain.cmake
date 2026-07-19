if(NOT DEFINED FORGE_CONTRACT_TOOLCHAIN_SELECTOR OR NOT EXISTS "${FORGE_CONTRACT_TOOLCHAIN_SELECTOR}")
   message(FATAL_ERROR "FORGE_CONTRACT_TOOLCHAIN_SELECTOR must name the developer toolchain selector")
endif()
if(NOT DEFINED FORGE_CONTRACT_TEST_ROOT OR FORGE_CONTRACT_TEST_ROOT STREQUAL "")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_ROOT is required")
endif()

set(_root "${FORGE_CONTRACT_TEST_ROOT}")
set(_selected "${_root}/selected/bin")
set(_path_toolchain "${_root}/path/bin")
set(_configured "${_root}/configured/bin")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_selected}" "${_path_toolchain}" "${_configured}")

set(_selected_tools clang++ clang-scan-deps llvm-ar llvm-ranlib)
if(NOT FORGE_CONTRACT_TEST_MISSING_WASM_LD)
   list(APPEND _selected_tools wasm-ld)
endif()
foreach(_tool IN LISTS _selected_tools)
   file(WRITE "${_selected}/${_tool}" "#!/bin/sh\nexit 0\n")
   file(
      CHMOD
      "${_selected}/${_tool}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
   )
endforeach()
foreach(_tool clang++ clang-scan-deps wasm-ld llvm-ar llvm-ranlib)
   file(WRITE "${_path_toolchain}/${_tool}" "#!/bin/sh\nexit 0\n")
   file(
      CHMOD
      "${_path_toolchain}/${_tool}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
   )
endforeach()
file(WRITE "${_configured}/clang-scan-deps" "#!/bin/sh\nexit 0\n")
file(
   CHMOD
   "${_configured}/clang-scan-deps"
   PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

set(ENV{PATH} "${_path_toolchain}:$ENV{PATH}")
set(LLVM_TOOLS_BINARY_DIR "${_selected}")
set(FORGE_CONTRACT_CLANG "")
set(FORGE_CONTRACT_CLANG_SCAN_DEPS "${_configured}/clang-scan-deps")
set(FORGE_CONTRACT_WASM_LD "")
set(FORGE_CONTRACT_LLVM_AR "")
set(FORGE_CONTRACT_LLVM_RANLIB "")

include("${FORGE_CONTRACT_TOOLCHAIN_SELECTOR}")
forge_contract_select_developer_toolchain()

if(FORGE_CONTRACT_TEST_MISSING_WASM_LD)
   return()
endif()

foreach(
   _binding
   "FORGE_CONTRACT_CLANG;clang++"
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

if(NOT FORGE_CONTRACT_CLANG_SCAN_DEPS STREQUAL "${_configured}/clang-scan-deps")
   message(FATAL_ERROR "FORGE_CONTRACT_CLANG_SCAN_DEPS did not preserve the configured path")
endif()

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -DFORGE_CONTRACT_TOOLCHAIN_SELECTOR=${FORGE_CONTRACT_TOOLCHAIN_SELECTOR}
      -DFORGE_CONTRACT_TEST_ROOT=${_root}/missing-wasm-ld
      -DFORGE_CONTRACT_TEST_MISSING_WASM_LD=ON
      -P "${CMAKE_CURRENT_LIST_FILE}"
   RESULT_VARIABLE _missing_wasm_ld_result
   OUTPUT_VARIABLE _missing_wasm_ld_output
   ERROR_VARIABLE _missing_wasm_ld_error
)
if(_missing_wasm_ld_result EQUAL 0)
   message(FATAL_ERROR "Developer toolchain selector accepted wasm-ld from PATH")
endif()
if(NOT "${_missing_wasm_ld_output}${_missing_wasm_ld_error}" MATCHES "wasm-ld")
   message(FATAL_ERROR "Developer toolchain selector failed for an unexpected reason")
endif()

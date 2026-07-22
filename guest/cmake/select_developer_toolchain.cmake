function(forge_contract_select_developer_toolchain)
   if(NOT FORGE_CONTRACT_CLANG)
      if(NOT LLVM_TOOLS_BINARY_DIR OR NOT IS_DIRECTORY "${LLVM_TOOLS_BINARY_DIR}")
         message(FATAL_ERROR "The selected LLVM package does not provide LLVM_TOOLS_BINARY_DIR")
      endif()
      find_program(
         _forge_contract_clang
         NAMES clang++
         HINTS "${LLVM_TOOLS_BINARY_DIR}"
         REQUIRED
         NO_DEFAULT_PATH
         NO_CACHE
      )
      set(FORGE_CONTRACT_CLANG "${_forge_contract_clang}")
   endif()

   get_filename_component(_forge_contract_clang_bin "${FORGE_CONTRACT_CLANG}" DIRECTORY)
   if(NOT FORGE_CONTRACT_CLANG_C)
      get_filename_component(_forge_contract_clang_name "${FORGE_CONTRACT_CLANG}" NAME)
      string(REGEX REPLACE "^clang\\+\\+" "clang" _forge_contract_clang_c_name "${_forge_contract_clang_name}")
      find_program(
         _forge_contract_clang_c
         NAMES "${_forge_contract_clang_c_name}"
         HINTS "${_forge_contract_clang_bin}"
         REQUIRED
         NO_DEFAULT_PATH
         NO_CACHE
      )
      set(FORGE_CONTRACT_CLANG_C "${_forge_contract_clang_c}")
   endif()
   if(NOT EXISTS "${FORGE_CONTRACT_CLANG_C}" OR IS_DIRECTORY "${FORGE_CONTRACT_CLANG_C}")
      message(FATAL_ERROR "Developer profile requires a valid FORGE_CONTRACT_CLANG_C: ${FORGE_CONTRACT_CLANG_C}")
   endif()

   if(NOT FORGE_CONTRACT_CLANG_SCAN_DEPS)
      find_program(
         _forge_contract_clang_scan_deps
         NAMES clang-scan-deps
         HINTS "${_forge_contract_clang_bin}"
         REQUIRED
         NO_DEFAULT_PATH
         NO_CACHE
      )
      set(FORGE_CONTRACT_CLANG_SCAN_DEPS "${_forge_contract_clang_scan_deps}")
   endif()
   if(NOT EXISTS "${FORGE_CONTRACT_CLANG_SCAN_DEPS}" OR IS_DIRECTORY "${FORGE_CONTRACT_CLANG_SCAN_DEPS}")
      message(FATAL_ERROR "Developer profile requires a valid FORGE_CONTRACT_CLANG_SCAN_DEPS: "
                          "${FORGE_CONTRACT_CLANG_SCAN_DEPS}")
   endif()

   if(NOT FORGE_CONTRACT_WASM_LD)
      find_program(
         _forge_contract_wasm_ld
         NAMES wasm-ld
         HINTS "${_forge_contract_clang_bin}"
         REQUIRED
         NO_DEFAULT_PATH
         NO_CACHE
      )
      set(FORGE_CONTRACT_WASM_LD "${_forge_contract_wasm_ld}")
   endif()
   if(NOT EXISTS "${FORGE_CONTRACT_LLVM_AR}")
      find_program(
         _forge_contract_llvm_ar
         NAMES llvm-ar
         HINTS "${_forge_contract_clang_bin}"
         REQUIRED
         NO_DEFAULT_PATH
         NO_CACHE
      )
      set(FORGE_CONTRACT_LLVM_AR "${_forge_contract_llvm_ar}")
   endif()
   if(NOT EXISTS "${FORGE_CONTRACT_LLVM_RANLIB}")
      find_program(
         _forge_contract_llvm_ranlib
         NAMES llvm-ranlib
         HINTS "${_forge_contract_clang_bin}"
         REQUIRED
         NO_DEFAULT_PATH
         NO_CACHE
      )
      set(FORGE_CONTRACT_LLVM_RANLIB "${_forge_contract_llvm_ranlib}")
   endif()

   set(FORGE_CONTRACT_CLANG "${FORGE_CONTRACT_CLANG}" PARENT_SCOPE)
   set(FORGE_CONTRACT_CLANG_C "${FORGE_CONTRACT_CLANG_C}" PARENT_SCOPE)
   set(FORGE_CONTRACT_CLANG_SCAN_DEPS "${FORGE_CONTRACT_CLANG_SCAN_DEPS}" PARENT_SCOPE)
   set(FORGE_CONTRACT_WASM_LD "${FORGE_CONTRACT_WASM_LD}" PARENT_SCOPE)
   set(FORGE_CONTRACT_LLVM_AR "${FORGE_CONTRACT_LLVM_AR}" PARENT_SCOPE)
   set(FORGE_CONTRACT_LLVM_RANLIB "${FORGE_CONTRACT_LLVM_RANLIB}" PARENT_SCOPE)
endfunction()

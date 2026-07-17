if(NOT DEFINED FORGE_CONTRACT_RUNTIME_POLICY)
   message(FATAL_ERROR "FORGE_CONTRACT_RUNTIME_POLICY is required")
endif()

include("${FORGE_CONTRACT_RUNTIME_POLICY}")

set(_runtime_test_root "${CMAKE_CURRENT_BINARY_DIR}/contract-runtime-search")
file(REMOVE_RECURSE "${_runtime_test_root}")
file(MAKE_DIRECTORY "${_runtime_test_root}/prefix/lib" "${_runtime_test_root}/prefix/lib64")
forge_contract_sdk_runtime_search_directories(
   _runtime_directories
   INSTALL_LIBRARY_DIR "${_runtime_test_root}/sdk/lib"
   PREFIXES "${_runtime_test_root}/prefix" "${_runtime_test_root}/missing"
   DIRECTORIES "${_runtime_test_root}/custom" "${_runtime_test_root}/custom"
)
foreach(
   _expected
   "${_runtime_test_root}/sdk/lib"
   "${_runtime_test_root}/prefix/lib"
   "${_runtime_test_root}/prefix/lib64"
   "${_runtime_test_root}/custom"
)
   list(FIND _runtime_directories "${_expected}" _position)
   if(_position EQUAL -1)
      message(FATAL_ERROR "runtime dependency search directory is missing: ${_expected}")
   endif()
endforeach()
list(LENGTH _runtime_directories _runtime_directory_count)
if(NOT _runtime_directory_count EQUAL 4)
   message(FATAL_ERROR "runtime dependency search directories were not normalized: ${_runtime_directories}")
endif()

foreach(
   _dependency
   "libc++.so.1"
   "/usr/lib/llvm-22/lib/libc++abi.so.1"
   "/lib/x86_64-linux-gnu/libunwind.so.1"
   "/usr/lib/llvm-22/lib/libLLVM.so.22.1"
   "/usr/lib/x86_64-linux-gnu/libLLVM-22.so.1"
   "/usr/lib/llvm-22/lib/libclang-cpp.so.22.1"
   "/usr/lib/llvm-22/lib/liblldWasm.so.22.1"
)
   if(NOT _dependency MATCHES "${FORGE_CONTRACT_LLVM_RUNTIME_REGEX}")
      message(FATAL_ERROR "LLVM runtime is not selected for bundling: ${_dependency}")
   endif()
endforeach()

if(NOT "libstdc++.so.6" MATCHES "${FORGE_CONTRACT_LINUX_CXX_RUNTIME_REGEX}")
   message(FATAL_ERROR "Linux C++ runtime is not selected for bundling")
endif()

foreach(_dependency "libc.so.6" "/usr/lib/x86_64-linux-gnu/libm.so.6")
   if(_dependency MATCHES "${FORGE_CONTRACT_LLVM_RUNTIME_REGEX}")
      message(FATAL_ERROR "system runtime is incorrectly selected for bundling: ${_dependency}")
   endif()
endforeach()

set(UNIX TRUE)
set(APPLE FALSE)
forge_contract_sdk_runtime_dependency_filters(_filters)
list(FIND _filters PRE_INCLUDE_REGEXES _include_position)
list(FIND _filters PRE_EXCLUDE_REGEXES _exclude_position)
if(_include_position EQUAL -1 OR _exclude_position EQUAL -1 OR _include_position GREATER _exclude_position)
   message(FATAL_ERROR "Linux LLVM C++ runtime include policy must precede system exclusions")
endif()

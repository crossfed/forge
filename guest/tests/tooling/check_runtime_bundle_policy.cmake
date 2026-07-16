if(NOT DEFINED FORGE_CONTRACT_RUNTIME_POLICY)
   message(FATAL_ERROR "FORGE_CONTRACT_RUNTIME_POLICY is required")
endif()

include("${FORGE_CONTRACT_RUNTIME_POLICY}")

foreach(
   _dependency
   "libc++.so.1"
   "/usr/lib/llvm-22/lib/libc++abi.so.1"
   "/lib/x86_64-linux-gnu/libunwind.so.1"
)
   if(NOT _dependency MATCHES "${FORGE_CONTRACT_LLVM_CXX_RUNTIME_REGEX}")
      message(FATAL_ERROR "LLVM C++ runtime is not selected for bundling: ${_dependency}")
   endif()
endforeach()

foreach(_dependency "libc.so.6" "/usr/lib/x86_64-linux-gnu/libm.so.6" "libstdc++.so.6")
   if(_dependency MATCHES "${FORGE_CONTRACT_LLVM_CXX_RUNTIME_REGEX}")
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

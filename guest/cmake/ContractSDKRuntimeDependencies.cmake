set(
   FORGE_CONTRACT_LLVM_RUNTIME_REGEX
   "(^|.*/)lib(c\\+\\+|c\\+\\+abi|unwind|LLVM(-[0-9]+)?|clang-cpp|lld[A-Za-z0-9_-]*)\\.so(\\.[0-9]+)*$"
)
set(
   FORGE_CONTRACT_LINUX_CXX_RUNTIME_REGEX
   "(^|.*/)libstdc\\+\\+\\.so(\\.[0-9]+)*$"
)

function(forge_contract_sdk_runtime_dependency_filters output)
   set(
      _filters
      PRE_EXCLUDE_REGEXES
         "^/System/Library/"
         "^/usr/lib/"
         "^/lib/"
   )
   if(UNIX AND NOT APPLE)
      # LLVM runtimes are SDK dependencies even when the selected distribution
      # installs them under /usr/lib. The selected libstdc++ is packaged as
      # well because Forge requires C++23 library facilities newer than the
      # baseline Ubuntu runtime.
      list(
         PREPEND _filters
         PRE_INCLUDE_REGEXES
            "${FORGE_CONTRACT_LLVM_RUNTIME_REGEX}"
            "${FORGE_CONTRACT_LINUX_CXX_RUNTIME_REGEX}"
      )
   endif()
   set(${output} "${_filters}" PARENT_SCOPE)
endfunction()

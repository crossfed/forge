set(
   FORGE_CONTRACT_LLVM_CXX_RUNTIME_REGEX
   "(^|.*/)lib(c\\+\\+|c\\+\\+abi|unwind)\\.so(\\.[0-9]+)*$"
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
      # LLVM's libc++, libc++abi, and libunwind are SDK runtime dependencies,
      # even when the selected LLVM distribution installs them under /usr/lib.
      list(PREPEND _filters PRE_INCLUDE_REGEXES "${FORGE_CONTRACT_LLVM_CXX_RUNTIME_REGEX}")
   endif()
   set(${output} "${_filters}" PARENT_SCOPE)
endfunction()

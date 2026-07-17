set(
   FORGE_CONTRACT_LLVM_RUNTIME_REGEX
   "(^|.*/)lib(c\\+\\+|c\\+\\+abi|unwind|LLVM(-[0-9]+)?|clang-cpp|lld[A-Za-z0-9_-]*)\\.so(\\.[0-9]+)*$"
)
set(
   FORGE_CONTRACT_LINUX_CXX_RUNTIME_REGEX
   "(^|.*/)libstdc\\+\\+\\.so(\\.[0-9]+)*$"
)

function(forge_contract_sdk_runtime_search_directories output)
   set(options)
   set(one_value_args INSTALL_LIBRARY_DIR)
   set(multi_value_args PREFIXES DIRECTORIES)
   cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

   set(_directories ${ARG_DIRECTORIES})
   if(ARG_INSTALL_LIBRARY_DIR)
      list(APPEND _directories "${ARG_INSTALL_LIBRARY_DIR}")
   endif()
   foreach(_prefix IN LISTS ARG_PREFIXES)
      foreach(_suffix lib lib64)
         if(IS_DIRECTORY "${_prefix}/${_suffix}")
            list(APPEND _directories "${_prefix}/${_suffix}")
         endif()
      endforeach()
   endforeach()
   list(FILTER _directories EXCLUDE REGEX "^$")
   list(REMOVE_DUPLICATES _directories)
   set(${output} "${_directories}" PARENT_SCOPE)
endfunction()

function(forge_contract_sdk_prepare_runtime_dependency path)
   if(NOT EXISTS "${path}")
      message(FATAL_ERROR "runtime dependency does not exist: ${path}")
   endif()
   file(
      CHMOD "${path}"
      PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
   )
endfunction()

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

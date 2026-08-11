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

set(_runtime_dependency "${_runtime_test_root}/readonly-runtime")
file(WRITE "${_runtime_dependency}" "runtime")
file(CHMOD "${_runtime_dependency}" PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)
forge_contract_sdk_prepare_runtime_dependency("${_runtime_dependency}")
file(APPEND "${_runtime_dependency}" "-writable")

set(
   _resolved_dependencies
   "/opt/homebrew/Cellar/llvm/22.1.8/lib/libLLVM.dylib"
   "/opt/homebrew/Cellar/llvm/22.1.8/lib/libclang-cpp.dylib"
)
forge_contract_sdk_match_runtime_dependency(
   _matched_dependency
   "/opt/homebrew/opt/llvm/lib/libLLVM.dylib"
   ${_resolved_dependencies}
)
if(NOT _matched_dependency STREQUAL "/opt/homebrew/Cellar/llvm/22.1.8/lib/libLLVM.dylib")
   message(FATAL_ERROR "runtime dependency symlink path was not matched to its bundled library")
endif()
forge_contract_sdk_match_runtime_dependency(
   _missing_dependency
   "/opt/homebrew/opt/llvm/lib/libMissing.dylib"
   ${_resolved_dependencies}
)
if(_missing_dependency)
   message(FATAL_ERROR "unrelated runtime dependency matched a bundled library")
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
list(FIND _filters POST_INCLUDE_REGEXES _post_include_position)
list(FIND _filters POST_EXCLUDE_REGEXES _exclude_position)
list(FIND _filters PRE_EXCLUDE_REGEXES _pre_exclude_position)
if(_include_position EQUAL -1 OR _post_include_position EQUAL -1 OR _exclude_position EQUAL -1)
   message(FATAL_ERROR "Linux runtime dependency filters are incomplete: ${_filters}")
endif()
if(NOT _pre_exclude_position EQUAL -1)
   message(FATAL_ERROR "resolved system paths must not use PRE_EXCLUDE_REGEXES: ${_filters}")
endif()
if(_post_include_position GREATER _exclude_position)
   message(FATAL_ERROR "Linux LLVM C++ runtime post-includes must precede system exclusions")
endif()

foreach(
   _required
   "${FORGE_CONTRACT_LLVM_RUNTIME_REGEX}"
   "${FORGE_CONTRACT_LINUX_CXX_RUNTIME_REGEX}"
)
   set(_positions)
   set(_index 0)
   foreach(_filter IN LISTS _filters)
      if(_filter STREQUAL _required)
         list(APPEND _positions ${_index})
      endif()
      math(EXPR _index "${_index} + 1")
   endforeach()
   list(LENGTH _positions _position_count)
   if(NOT _position_count EQUAL 2)
      message(FATAL_ERROR "runtime filter must be present in both include phases: ${_required}")
   endif()
   list(GET _positions 0 _first_position)
   list(GET _positions 1 _second_position)
   if(_first_position LESS_EQUAL _include_position OR _first_position GREATER_EQUAL _post_include_position)
      message(FATAL_ERROR "runtime filter is missing from PRE_INCLUDE_REGEXES: ${_required}")
   endif()
   if(_second_position LESS_EQUAL _post_include_position OR _second_position GREATER_EQUAL _exclude_position)
      message(FATAL_ERROR "runtime filter is missing from POST_INCLUDE_REGEXES: ${_required}")
   endif()
endforeach()

foreach(_system_path "^/System/Library/" "^/usr/lib/" "^/lib/")
   list(FIND _filters "${_system_path}" _position)
   if(_position LESS _exclude_position)
      message(FATAL_ERROR "system path is not a post-resolution exclusion: ${_system_path}")
   endif()
endforeach()

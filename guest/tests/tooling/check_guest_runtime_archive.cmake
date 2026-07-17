foreach(
   _required
   FORGE_CONTRACT_STAGE_SYSROOT
   FORGE_CONTRACT_SDK_CMAKE
   FORGE_CONTRACT_BUILD_CMAKE
   FORGE_CONTRACT_RUNTIME_CMAKE
   FORGE_CONTRACT_CONFIG_TEMPLATE
)
   if(NOT DEFINED ${_required} OR NOT EXISTS "${${_required}}")
      message(FATAL_ERROR "${_required} must name an existing file")
   endif()
endforeach()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/contract-guest-runtime-policy")
set(_source "${_root}/source")
set(_destination "${_root}/destination")
set(_stamp "${_root}/stage.stamp")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_source}/include" "${_source}/lib")
file(WRITE "${_source}/include/source-marker.hpp" "source\n")
file(WRITE "${_source}/lib/libc++.a" "libcxx\n")

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -DSOURCE=${_source}
      -DDESTINATION=${_destination}
      -DSTAMP=${_stamp}
      -P "${FORGE_CONTRACT_STAGE_SYSROOT}"
   COMMAND_ERROR_IS_FATAL ANY
)
if(NOT EXISTS "${_destination}/include/source-marker.hpp" OR NOT EXISTS "${_destination}/lib/libc++.a")
   message(FATAL_ERROR "staged sysroot is incomplete")
endif()
file(WRITE "${_destination}/include/staged-only.hpp" "staged\n")
if(EXISTS "${_source}/include/staged-only.hpp")
   message(FATAL_ERROR "staging mutated the developer input sysroot")
endif()
file(READ "${_source}/include/source-marker.hpp" _source_marker)
if(NOT _source_marker STREQUAL "source\n")
   message(FATAL_ERROR "staging changed a developer sysroot file")
endif()

function(_require_text file text)
   file(READ "${file}" _contents)
   string(FIND "${_contents}" "${text}" _position)
   if(_position EQUAL -1)
      message(FATAL_ERROR "${file} is missing required guest runtime policy: ${text}")
   endif()
endfunction()

function(_reject_text file text)
   file(READ "${file}" _contents)
   string(FIND "${_contents}" "${text}" _position)
   if(NOT _position EQUAL -1)
      message(FATAL_ERROR "${file} contains forbidden guest runtime policy: ${text}")
   endif()
endfunction()

_require_text("${FORGE_CONTRACT_SDK_CMAKE}" "ExternalProject_Add(\n   forge_contract_guest_runtime")
_require_text("${FORGE_CONTRACT_SDK_CMAKE}" "DEPENDS forge_contract_guest_runtime")
_require_text("${FORGE_CONTRACT_RUNTIME_CMAKE}" "install(TARGETS forge_guest_runtime ARCHIVE")
_require_text("${FORGE_CONTRACT_BUILD_CMAKE}" "add_library(forge_guest_runtime STATIC IMPORTED GLOBAL)")
_require_text("${FORGE_CONTRACT_BUILD_CMAKE}" "set(_guest_libraries forge_guest_runtime")
_require_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "ForgeContract_RUNTIME_ARCHIVE")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/allocator.cpp")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/memory.cpp")

foreach(
   _required
   FORGE_CONTRACT_STAGE_SYSROOT
   FORGE_CONTRACT_SDK_CMAKE
   FORGE_CONTRACT_LIBRARIES_CMAKE
   FORGE_CONTRACT_BUILD_CMAKE
   FORGE_CONTRACT_FOUNDATION_CMAKE
   FORGE_CONTRACT_CONFIG_TEMPLATE
   FORGE_CONTRACT_GUEST_COMPONENTS
)
   if(NOT DEFINED ${_required} OR NOT EXISTS "${${_required}}")
      message(FATAL_ERROR "${_required} must name an existing file")
   endif()
endforeach()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/contract-guest-runtime-policy")
set(_source "${_root}/source")
set(_destination "${_root}/destination")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_source}/include" "${_source}/lib")
file(WRITE "${_source}/include/source-marker.hpp" "source\n")
file(WRITE "${_source}/lib/libc++.a" "libcxx\n")

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -DSOURCE=${_source}
      -DDESTINATION=${_destination}
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

file(WRITE "${_source}/include/source-marker.hpp" "updated\n")
file(WRITE "${_source}/include/added.hpp" "added\n")
file(REMOVE "${_source}/lib/libc++.a")
file(WRITE "${_source}/lib/libc++abi.a" "libcxxabi\n")
execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -DSOURCE=${_source}
      -DDESTINATION=${_destination}
      -P "${FORGE_CONTRACT_STAGE_SYSROOT}"
   COMMAND_ERROR_IS_FATAL ANY
)
file(READ "${_destination}/include/source-marker.hpp" _updated_marker)
if(NOT _updated_marker STREQUAL "updated\n" OR NOT EXISTS "${_destination}/include/added.hpp")
   message(FATAL_ERROR "restaging did not copy changed developer sysroot files")
endif()
if(EXISTS "${_destination}/include/staged-only.hpp" OR EXISTS "${_destination}/lib/libc++.a")
   message(FATAL_ERROR "restaging retained files absent from the developer sysroot")
endif()
if(NOT EXISTS "${_destination}/lib/libc++abi.a")
   message(FATAL_ERROR "restaging did not copy a new developer sysroot library")
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

_require_text("${FORGE_CONTRACT_SDK_CMAKE}" "ExternalProject_Add(\n   forge_contract_guest_foundation")
_require_text("${FORGE_CONTRACT_SDK_CMAKE}" "SOURCE_DIR \"\${CMAKE_CURRENT_SOURCE_DIR}/cmake/foundation-build\"")
_require_text(
   "${FORGE_CONTRACT_SDK_CMAKE}"
   "add_custom_target(\n   forge_contract_foundation_manifest ALL"
)
_require_text("${FORGE_CONTRACT_SDK_CMAKE}" "BYPRODUCTS \"\${_foundation_manifest}\"")
_reject_text("${FORGE_CONTRACT_SDK_CMAKE}" "OUTPUT \"\${_foundation_manifest}\"")
_require_text("${FORGE_CONTRACT_SDK_CMAKE}" "add_custom_target(\n   forge_contract_stage_sysroot")
_reject_text("${FORGE_CONTRACT_SDK_CMAKE}" "forge-contract-sysroot.stamp")
_reject_text("${FORGE_CONTRACT_SDK_CMAKE}" "forge_contract_guest_runtime")
_reject_text("${FORGE_CONTRACT_SDK_CMAKE}" "forge_contract_guest_codecs")
foreach(_archive IN ITEMS
   forge_guest_runtime
   forge_guest_raw
   forge_guest_codec_base64
   forge_guest_codec_base58
   forge_guest_codec_hex
   forge_guest_chain_protocol
   forge_guest_contract
   forge_guest_math
)
   _require_text("${FORGE_CONTRACT_FOUNDATION_CMAKE}" "${_archive}")
endforeach()
_require_text("${FORGE_CONTRACT_LIBRARIES_CMAKE}" "function(forge_add_contract_library target)")
_require_text("${FORGE_CONTRACT_LIBRARIES_CMAKE}" "FILE_SET forge_contract_modules TYPE CXX_MODULES")
_require_text("${FORGE_CONTRACT_LIBRARIES_CMAKE}" "add_library(\"\${_target}_archive\" STATIC IMPORTED GLOBAL)")
_require_text("${FORGE_CONTRACT_LIBRARIES_CMAKE}" "FORGE_CONTRACT_FOUNDATION_COMPONENT_IDS")
_require_text("${FORGE_CONTRACT_GUEST_COMPONENTS}" "ID forge.contract.runtime")
_require_text("${FORGE_CONTRACT_GUEST_COMPONENTS}" "ARCHIVE libforge_guest_contract.a")
_require_text(
   "${FORGE_CONTRACT_LIBRARIES_CMAKE}"
   "These values are compared verbatim only. Forge never interprets them as"
)
_reject_text("${FORGE_CONTRACT_LIBRARIES_CMAKE}" "LINK_ONLY")
_reject_text("${FORGE_CONTRACT_LIBRARIES_CMAKE}" "contract-graph.json")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "forge_guest_codec_base64_runtime")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "forge_guest_raw_implementation")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "forge_guest_chain_protocol_implementation")
_require_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "foundation.json")
_require_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "file(SHA256")
_reject_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "ForgeContract_RUNTIME_ARCHIVE")
_reject_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "ForgeContract_CODEC_BASE64_ARCHIVE")
_reject_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "ForgeContract_CODEC_BASE58_ARCHIVE")
_reject_text("${FORGE_CONTRACT_CONFIG_TEMPLATE}" "ForgeContract_CODEC_HEX_ARCHIVE")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/allocator.cpp")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/memory.cpp")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/base64.cpp")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/base58.cpp")
_reject_text("${FORGE_CONTRACT_BUILD_CMAKE}" "\${_runtime}/hex.cpp")

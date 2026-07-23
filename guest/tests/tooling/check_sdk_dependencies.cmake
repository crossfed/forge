if(NOT DEFINED FORGE_CONTRACT_SDK_CMAKE OR NOT EXISTS "${FORGE_CONTRACT_SDK_CMAKE}")
   message(FATAL_ERROR "FORGE_CONTRACT_SDK_CMAKE must name an existing file")
endif()

file(READ "${FORGE_CONTRACT_SDK_CMAKE}" _sdk_cmake)

if(NOT _sdk_cmake MATCHES "find_package\\(Boost 1\\.90\\.0 REQUIRED CONFIG COMPONENTS headers\\)")
   message(FATAL_ERROR "Contract SDK must accept Boost 1.90.0 or newer")
endif()

if(_sdk_cmake MATCHES "find_package\\(Boost[^\\n]* EXACT")
   message(FATAL_ERROR "Contract SDK must not require an exact Boost release")
endif()

if(
   NOT _sdk_cmake
      MATCHES
      "if\\(FORGE_CONTRACT_FORGE_DIR\\)[ \t\r\n]+set\\(_forge_contract_host_package_dir \"\\$\\{FORGE_CONTRACT_FORGE_DIR\\}\"\\)"
)
   message(FATAL_ERROR "Contract SDK relocation must preserve the configured release Forge package")
endif()

if(_sdk_cmake MATCHES "--forge-package \"\\$\\{Forge_DIR\\}\"")
   message(FATAL_ERROR "Contract SDK relocation must not bypass the resolved Forge host package")
endif()

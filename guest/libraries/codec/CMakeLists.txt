cmake_minimum_required(VERSION 3.31)

project(ForgeGuestCodecs LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT FORGE_SOURCE_DIR OR NOT IS_DIRECTORY "${FORGE_SOURCE_DIR}/libraries/codec")
   message(FATAL_ERROR "FORGE_SOURCE_DIR must name the Forge source tree")
endif()
if(NOT FORGE_GUEST_INCLUDE_DIR OR NOT IS_DIRECTORY "${FORGE_GUEST_INCLUDE_DIR}")
   message(FATAL_ERROR "FORGE_GUEST_INCLUDE_DIR must contain generated contract headers")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

function(forge_add_guest_codec name)
   set(_source "${FORGE_SOURCE_DIR}/libraries/codec/${name}")

   add_library(forge_guest_codec_${name}_modules OBJECT)
   target_sources(
      forge_guest_codec_${name}_modules
      PUBLIC
         FILE_SET CXX_MODULES
         BASE_DIRS "${_source}/include"
         FILES
            "${_source}/include/forge/codec/${name}/exceptions.cppm"
            "${_source}/include/forge/codec/${name}/${name}.cppm"
   )

   add_library(forge_guest_codec_${name}_implementation OBJECT "${_source}/${name}.cpp")
   target_link_libraries(forge_guest_codec_${name}_implementation PRIVATE forge_guest_codec_${name}_modules)

   foreach(_target IN ITEMS forge_guest_codec_${name}_modules forge_guest_codec_${name}_implementation)
      target_include_directories(
         ${_target}
         PRIVATE
            "${FORGE_SOURCE_DIR}/libraries/exceptions/include"
            "${FORGE_GUEST_INCLUDE_DIR}"
      )
      target_compile_definitions(${_target} PRIVATE FORGE_CONTRACT_GUEST=1)
      target_compile_options(
         ${_target}
         PRIVATE
            -fno-exceptions
            -fno-rtti
            -fno-threadsafe-statics
            -ffreestanding
            -fvisibility=hidden
            -fno-ident
            -mcpu=mvp
      )
   endforeach()

   add_library(forge_guest_codec_${name} STATIC $<TARGET_OBJECTS:forge_guest_codec_${name}_implementation>)
   install(TARGETS forge_guest_codec_${name} ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endfunction()

forge_add_guest_codec(base64)
forge_add_guest_codec(base58)
forge_add_guest_codec(hex)

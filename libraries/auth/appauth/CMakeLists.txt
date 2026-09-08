if(NOT APPLE)
   return()
endif()

if(NOT TARGET AppAuth::AppAuth)
   message(STATUS "Skipping forge_auth_appauth: coordinator must provide AppAuth::AppAuth pinned to 3.0.0")
   return()
endif()

if(NOT DEFINED AppAuth_VERSION OR NOT AppAuth_VERSION VERSION_EQUAL "3.0.0")
   message(FATAL_ERROR "forge_auth_appauth requires AppAuth_VERSION 3.0.0")
endif()

find_library(FORGE_APPAUTH_SECURITY_FRAMEWORK Security REQUIRED)

enable_language(OBJCXX)

add_library(forge_auth_appauth STATIC provider.cpp provider_impl.cpp auth_state_binding.cpp appauth_backend.mm)
set_source_files_properties(appauth_backend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_target_properties(forge_auth_appauth PROPERTIES MACOSX_DEPLOYMENT_TARGET "12.0")
forge_target_modules_at(forge_auth_appauth auth/appauth)

target_include_directories(
   forge_auth_appauth
   PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS}
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_auth_appauth PUBLIC
      Boost::headers
      forge_auth_oauth2
      forge_crypto_core
      forge_exceptions
   PRIVATE
      AppAuth::AppAuth
      ${FORGE_APPAUTH_SECURITY_FRAMEWORK}
)

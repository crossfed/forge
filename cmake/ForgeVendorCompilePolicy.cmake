include_guard(GLOBAL)

function(forge_apply_vendored_implementation_policy target)
   if(NOT TARGET ${target})
      message(FATAL_ERROR "Unknown vendored implementation target: ${target}")
   endif()

   get_target_property(_forge_vendor_target_type ${target} TYPE)
   if(_forge_vendor_target_type STREQUAL "INTERFACE_LIBRARY")
      message(FATAL_ERROR "Vendored implementation policy requires a compiled target: ${target}")
   endif()

   # Target options follow parent configuration flags, so the final selector
   # optimizes vendor code without changing Forge Debug or sanitizer settings.
   target_compile_options(
      ${target}
      PRIVATE
         "$<$<AND:$<CONFIG:Debug>,$<OR:$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>,$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>>>:-O2>"
         "$<$<AND:$<CONFIG:Debug>,$<OR:$<COMPILE_LANG_AND_ID:C,MSVC>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>>:/O2>"
   )
   set_property(TARGET ${target} PROPERTY FORGE_VENDORED_IMPLEMENTATION_POLICY ON)
endfunction()

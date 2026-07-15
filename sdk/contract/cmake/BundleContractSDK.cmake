set(_prefix "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
set(_executables
   "${_prefix}/bin/forge-abigen"
   "${_prefix}/bin/forge-contract-check"
   "${_prefix}/bin/forge-contract-manifest"
)
set(_plugin "${_prefix}/lib/forge-contract/forge-attr-plugin${CMAKE_SHARED_MODULE_SUFFIX}")

foreach(_path IN LISTS _executables _plugin)
   if(NOT EXISTS "${_path}")
      message(FATAL_ERROR "Forge Contract SDK runtime bundling cannot find ${_path}")
   endif()
endforeach()

file(GET_RUNTIME_DEPENDENCIES
   RESOLVED_DEPENDENCIES_VAR _dependencies
   UNRESOLVED_DEPENDENCIES_VAR _unresolved
   EXECUTABLES ${_executables}
   LIBRARIES "${_plugin}"
   DIRECTORIES "${_prefix}/lib"
   PRE_EXCLUDE_REGEXES
      "^/System/Library/"
      "^/usr/lib/"
      "^/lib/"
)
if(_unresolved)
   message(FATAL_ERROR "unresolved Forge Contract SDK runtime dependencies: ${_unresolved}")
endif()

file(MAKE_DIRECTORY "${_prefix}/lib")
set(_bundled)
foreach(_dependency IN LISTS _dependencies)
   get_filename_component(_name "${_dependency}" NAME)
   set(_destination "${_prefix}/lib/${_name}")
   file(COPY_FILE "${_dependency}" "${_destination}" ONLY_IF_DIFFERENT)
   list(APPEND _bundled "${_destination}")
endforeach()

if(APPLE)
   find_program(_install_name_tool install_name_tool REQUIRED)
   find_program(_otool otool REQUIRED)
   set(_binaries ${_executables} "${_plugin}" ${_bundled})
   foreach(_binary IN LISTS _binaries)
      execute_process(COMMAND "${_otool}" -L "${_binary}" OUTPUT_VARIABLE _linked COMMAND_ERROR_IS_FATAL ANY)
      foreach(_dependency IN LISTS _dependencies)
         string(FIND "${_linked}" "${_dependency}" _found)
         if(NOT _found EQUAL -1)
            get_filename_component(_name "${_dependency}" NAME)
            execute_process(
               COMMAND "${_install_name_tool}" -change "${_dependency}" "@rpath/${_name}" "${_binary}"
               COMMAND_ERROR_IS_FATAL ANY
            )
         endif()
      endforeach()
   endforeach()
   foreach(_library IN LISTS _bundled)
      get_filename_component(_name "${_library}" NAME)
      execute_process(
         COMMAND "${_install_name_tool}" -id "@rpath/${_name}" "${_library}"
         COMMAND_ERROR_IS_FATAL ANY
      )
   endforeach()
endif()

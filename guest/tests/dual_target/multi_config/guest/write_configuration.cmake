if(NOT DEFINED OUTPUT OR NOT DEFINED CONFIGURATION)
   message(FATAL_ERROR "OUTPUT and CONFIGURATION are required")
endif()

get_filename_component(_output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
file(WRITE "${OUTPUT}" "${CONFIGURATION}\n")

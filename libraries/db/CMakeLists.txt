add_subdirectory(core)
add_subdirectory(object)
add_subdirectory(blob)

if(FORGE_HAS_ROCKSDB)
   add_subdirectory(rocksdb)
endif()

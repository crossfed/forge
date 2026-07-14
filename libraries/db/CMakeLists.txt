add_subdirectory(core)
add_subdirectory(object)
add_subdirectory(blob)
add_subdirectory(revision)

if(FORGE_HAS_ROCKSDB)
   add_subdirectory(rocksdb)
endif()

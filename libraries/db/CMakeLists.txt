add_subdirectory(core)
add_subdirectory(object)
add_subdirectory(blob)
add_subdirectory(revision)

if(FORGE_HAS_ROCKSDB)
   add_subdirectory(rocksdb)
endif()

if(FORGE_HAS_MDBX)
   add_subdirectory(mdbx)
endif()

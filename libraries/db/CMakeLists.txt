add_library(
   forge_db STATIC
   driver.cpp
   driver_snapshot.cpp
   driver_transaction.cpp
)
forge_target_modules(forge_db db)

target_include_directories(forge_db PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_db PUBLIC
      Boost::asio
      forge_exceptions
)

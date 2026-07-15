add_library(
   forge_db_core STATIC
   driver.cpp
   driver_state.cpp
   driver_snapshot.cpp
   driver_transaction.cpp
   participant_access_impl.cpp
   tracked_session.cpp
)
forge_target_modules_at(forge_db_core db/core)

target_include_directories(forge_db_core PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_db_core PUBLIC
      Boost::asio
      forge_exceptions
)

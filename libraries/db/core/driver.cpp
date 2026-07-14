module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <memory>

module forge.db.core.driver;

import forge.db.core.exceptions;

namespace forge::db::core {

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
session::get_for_update(family, record_key) {
   FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support record locks");
}

boost::asio::awaitable<void> session::create_savepoint() {
   FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support savepoints");
}

boost::asio::awaitable<void> session::rollback_to_savepoint() {
   FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support savepoints");
}

boost::asio::awaitable<void> session::release_savepoint() {
   FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support savepoints");
}

boost::asio::awaitable<transaction> driver::begin_transaction() {
   const auto executor = co_await boost::asio::this_coro::executor;
   co_return transaction{co_await open_transaction(), executor};
}

boost::asio::awaitable<snapshot> driver::begin_read() {
   co_return snapshot{co_await open_snapshot()};
}

} // namespace forge::db::core

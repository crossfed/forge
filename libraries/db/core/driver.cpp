module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <memory>

module forge.db.core.driver;

namespace forge::db::core {

boost::asio::awaitable<transaction> driver::begin_transaction() {
   const auto executor = co_await boost::asio::this_coro::executor;
   co_return transaction{co_await open_transaction(), executor};
}

boost::asio::awaitable<snapshot> driver::begin_read() {
   co_return snapshot{co_await open_snapshot()};
}

} // namespace forge::db::core

module;

#include <forge/exceptions/macros.hpp>
#include <mdbx.h>

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module forge.db.mdbx.driver;

import forge.asio.affine;
import forge.asio.gate;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.mdbx.exceptions;

#include "details/driver_impl.hxx"
#include "details/environment.hxx"

namespace forge::db::mdbx {

driver::driver(std::shared_ptr<detail::driver_impl> impl)
    : impl_{std::move(impl)} {}

driver::~driver() = default;

boost::asio::awaitable<std::shared_ptr<driver>>
driver::open(config value, forge::asio::affine::executor executor) {
   if (!executor.valid()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            "MDBX requires a valid affine executor");
   }

   auto opened = co_await executor.execute(
      {.name = "mdbx-open"},
      [value = std::move(value)]() mutable {
         return std::pair{detail::environment::open(value),
                          std::this_thread::get_id()};
      });
   auto impl = std::make_shared<detail::driver_impl>(
      std::move(executor), std::move(opened.first), opened.second);
   co_return std::shared_ptr<driver>{new driver{std::move(impl)}};
}

boost::asio::awaitable<void> driver::async_flush(bool sync) {
   co_await impl_->flush(sync);
}

boost::asio::awaitable<std::unique_ptr<forge::db::core::session>>
driver::open_transaction() {
   co_return co_await impl_->open_transaction();
}

boost::asio::awaitable<std::unique_ptr<forge::db::core::session>>
driver::open_snapshot() {
   co_return impl_->open_snapshot();
}

boost::asio::awaitable<void> driver::close_driver() {
   co_await impl_->close();
}

} // namespace forge::db::mdbx

module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

module forge.db.blob.store;

import forge.db.blob.exceptions;

#include "details/store_impl.hxx"

namespace forge::db::blob {

store::impl::impl(std::shared_ptr<forge::db::driver> driver_value, store::config config_value)
    : driver{std::move(driver_value)}, config{std::move(config_value)} {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db blob driver is null");
   }
   if (config.data_family.name.empty() || config.refs_family.name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db blob family names must not be empty");
   }
}

store::store(std::shared_ptr<forge::db::driver> driver)
    : store(std::move(driver), config{}) {}

store::store(std::shared_ptr<forge::db::driver> driver, config settings)
    : impl_{std::make_shared<impl>(std::move(driver), std::move(settings))} {}

boost::asio::awaitable<transaction> store::begin_transaction() {
   auto active = co_await impl_->driver->begin_transaction();
   co_return transaction{
      std::move(active),
      impl_->config.data_family,
      impl_->config.refs_family};
}

transaction store::join(forge::db::transaction& active) {
   return transaction{
      active,
      impl_->config.data_family,
      impl_->config.refs_family};
}

boost::asio::awaitable<ref<digest>> store::put(std::vector<std::byte> payload) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto value = ref<digest>{};
   try {
      value = co_await active.put(std::move(payload));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
   co_return value;
}

boost::asio::awaitable<collect_result> store::collect_unreferenced(collect_options options) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto result = collect_result{};
   try {
      result = co_await active.collect_unreferenced(options);
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
   co_return result;
}

} // namespace forge::db::blob

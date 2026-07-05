module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

module forge.blobdb.store;

import forge.blobdb.exceptions;

#include "details/store_impl.hxx"

namespace forge::blobdb {

store::impl::impl(std::shared_ptr<forge::db::driver> driver_value, store::config config_value)
    : driver{std::move(driver_value)}, config{std::move(config_value)} {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blobdb driver is null");
   }
   if (config.data_family.name.empty() || config.refs_family.name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blobdb family names must not be empty");
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
      impl_->config.refs_family,
      impl_->config.digest_hasher,
      impl_->config.verify_on_write,
      impl_->config.verify_on_read};
}

transaction store::join(forge::db::transaction& active) {
   return transaction{
      active,
      impl_->config.data_family,
      impl_->config.refs_family,
      impl_->config.digest_hasher,
      impl_->config.verify_on_write,
      impl_->config.verify_on_read};
}

boost::asio::awaitable<digest> store::put(std::vector<std::byte> bytes) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto id = digest{};
   try {
      id = co_await active.put(std::move(bytes));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
   co_return id;
}

boost::asio::awaitable<void> store::put(digest id, std::vector<std::byte> bytes) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.put(std::move(id), std::move(bytes));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

boost::asio::awaitable<std::vector<std::byte>> store::get(digest id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto bytes = std::vector<std::byte>{};
   try {
      bytes = co_await active.get(std::move(id));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
   co_return bytes;
}

boost::asio::awaitable<bool> store::has(digest id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto exists = false;
   try {
      exists = co_await active.has(std::move(id));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
   co_return exists;
}

boost::asio::awaitable<stat> store::stat_blob(digest id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto value = stat{};
   try {
      value = co_await active.stat_blob(std::move(id));
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

boost::asio::awaitable<void> store::erase(digest id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.erase(std::move(id));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

boost::asio::awaitable<void> store::verify(digest id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.verify(std::move(id));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

boost::asio::awaitable<void> store::retain(digest id, owner_ref owner) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.retain(std::move(id), std::move(owner));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

boost::asio::awaitable<void> store::release(digest id, owner_ref owner) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.release(std::move(id), std::move(owner));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

boost::asio::awaitable<std::uint64_t> store::ref_count(digest id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   auto count = std::uint64_t{};
   try {
      count = co_await active.ref_count(std::move(id));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
   co_return count;
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

} // namespace forge::blobdb

module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <memory>
#include <string>
#include <utility>

module forge.plugins.db.store.plugin;

import forge.db.blob.store;
import forge.db.object.hooks;
import forge.db.object.snapshot;
import forge.db.object.store;
import forge.db.object.transaction;
import forge.db.core.driver;
import forge.plugins.db.store.exceptions;
import forge.plugins.db.store.types;

#include "details/plugin_impl.hxx"

namespace forge::plugins::db::store {

class plugin::api_impl::handle_state final : public store_handle_state {
 public:
   handle_state(std::weak_ptr<impl> owner, std::string name) : owner_{std::move(owner)}, name_{std::move(name)} {}

   [[nodiscard]] std::string name() const override {
      return name_;
   }

   [[nodiscard]] std::shared_ptr<forge::db::core::driver> require_driver() const override {
      const auto owner = owner_.lock();
      if (!owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin is stopped");
      }

      return owner->require_open_store(name_).driver;
   }

   [[nodiscard]] std::shared_ptr<forge::db::object::store> require_objects() const override {
      const auto owner = owner_.lock();
      if (!owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin is stopped");
      }

      auto opened = owner->require_open_store(name_);
      if (!opened.objects) {
         FORGE_THROW_EXCEPTION(exceptions::unavailable_layer, "db store object layer is not configured",
                               forge::exceptions::ctx("store", name_));
      }
      return opened.objects;
   }

   [[nodiscard]] std::shared_ptr<forge::db::blob::store> require_blobs() const override {
      const auto owner = owner_.lock();
      if (!owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin is stopped");
      }

      auto opened = owner->require_open_store(name_);
      if (!opened.blobs) {
         FORGE_THROW_EXCEPTION(exceptions::unavailable_layer, "db store blob layer is not configured",
                               forge::exceptions::ctx("store", name_));
      }
      return opened.blobs;
   }

   boost::asio::awaitable<transaction> begin_transaction() const override {
      const auto owner = owner_.lock();
      if (!owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin is stopped");
      }

      auto opened = owner->require_open_store(name_);
      if (opened.objects) {
         co_return transaction{co_await opened.objects->begin_transaction()};
      }
      co_return transaction{co_await opened.driver->begin_transaction()};
   }

 private:
   std::weak_ptr<impl> owner_;
   std::string name_;
};

plugin::api_impl::api_impl(std::shared_ptr<impl> owner) : owner_{std::move(owner)} {}

boost::asio::awaitable<void>
plugin::api_impl::add_store(std::string name,
                            std::shared_ptr<forge::db::core::driver> driver,
                            store_options options) {
   owner_->add_store(std::move(name), std::move(driver), options);
   co_return;
}

boost::asio::awaitable<store_handle> plugin::api_impl::store(std::string name) {
   (void)owner_->require_store(name);
   co_return store_handle{std::make_shared<handle_state>(owner_, std::move(name))};
}

boost::asio::awaitable<void> plugin::api_impl::flush(std::string name, bool sync) {
   auto state = std::make_shared<handle_state>(owner_, std::move(name));
   co_await state->require_driver()->async_flush(sync);
}

boost::asio::awaitable<void> plugin::api_impl::flush_all(bool sync) {
   for (const auto& item : owner_->current_status().stores) {
      auto state = std::make_shared<handle_state>(owner_, item.name);
      co_await state->require_driver()->async_flush(sync);
   }
}

boost::asio::awaitable<::forge::plugins::db::store::status> plugin::api_impl::status() {
   co_return owner_->current_status();
}

} // namespace forge::plugins::db::store

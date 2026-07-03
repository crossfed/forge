module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <memory>
#include <string>
#include <utility>

module forge.plugins.db.objectdb.plugin;

import forge.objectdb.driver;
import forge.objectdb.hooks;
import forge.objectdb.snapshot;
import forge.objectdb.store;
import forge.objectdb.transaction;
import forge.plugins.db.objectdb.exceptions;
import forge.plugins.db.objectdb.types;

#include "details/plugin_impl.hxx"

namespace forge::plugins::db::objectdb {

class plugin::api_impl::handle_state final : public store_handle_state {
 public:
   handle_state(std::weak_ptr<impl> owner, std::string name) : owner_{std::move(owner)}, name_{std::move(name)} {}

   [[nodiscard]] std::string name() const override {
      return name_;
   }

   [[nodiscard]] std::shared_ptr<forge::objectdb::store> require_store() const override {
      const auto owner = owner_.lock();
      if (!owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb plugin is stopped");
      }

      return owner->require_open_store(name_).store;
   }

   [[nodiscard]] std::shared_ptr<forge::objectdb::driver> require_driver() const override {
      const auto owner = owner_.lock();
      if (!owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb plugin is stopped");
      }

      return owner->require_open_store(name_).driver;
   }

 private:
   std::weak_ptr<impl> owner_;
   std::string name_;
};

plugin::api_impl::api_impl(std::shared_ptr<impl> owner) : owner_{std::move(owner)} {}

boost::asio::awaitable<void>
plugin::api_impl::add_store(std::string name,
                            std::shared_ptr<forge::objectdb::driver> driver,
                            forge::objectdb::store::options options) {
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

boost::asio::awaitable<::forge::plugins::db::objectdb::status> plugin::api_impl::status() {
   co_return owner_->current_status();
}

} // namespace forge::plugins::db::objectdb

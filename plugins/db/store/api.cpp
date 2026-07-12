module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.db.store.api;

import forge.db.blob.ref;
import forge.db.blob.store;
import forge.db.blob.transaction;
import forge.db.blob.types;
import forge.db.core.driver;
import forge.db.object.hooks;
import forge.db.object.snapshot;
import forge.db.object.store;
import forge.db.object.transaction;
import forge.plugins.db.store.exceptions;

namespace forge::plugins::db::store {

transaction::transaction(forge::db::core::transaction active) : core_{std::move(active)} {}

transaction::transaction(forge::db::object::transaction active) : object_{std::move(active)} {}

bool transaction::active() const noexcept {
   return object_.has_value() || (core_.has_value() && core_->active());
}

forge::db::core::transaction& transaction::db_transaction() {
   if (object_.has_value()) {
      return object_->db_transaction();
   }
   if (core_.has_value()) {
      return *core_;
   }
   FORGE_THROW_EXCEPTION(exceptions::stopped, "db store transaction is closed");
}

boost::asio::awaitable<void> transaction::commit() {
   if (object_.has_value()) {
      co_await object_->commit();
      object_.reset();
      co_return;
   }
   if (core_.has_value()) {
      co_await core_->commit();
      core_.reset();
   }
}

boost::asio::awaitable<void> transaction::rollback() {
   if (object_.has_value()) {
      co_await object_->rollback();
      object_.reset();
      co_return;
   }
   if (core_.has_value()) {
      co_await core_->rollback();
      core_.reset();
   }
}

std::string object_handle::name() const {
   if (!state_) {
      return {};
   }
   return state_->name();
}

std::shared_ptr<forge::db::object::store> object_handle::require_setup_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store object handle is empty");
   }
   return state_->require_objects_for_setup();
}

std::shared_ptr<forge::db::object::store> object_handle::require_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store object handle is empty");
   }
   return state_->require_objects();
}

void object_handle::add_interceptor(std::shared_ptr<forge::db::object::interceptor> value) const {
   require_setup_store()->add_interceptor(std::move(value));
}

void object_handle::add_observer(std::shared_ptr<forge::db::object::observer> value) const {
   require_setup_store()->add_observer(std::move(value));
}

boost::asio::awaitable<forge::db::object::transaction> object_handle::begin_transaction() const {
   co_return co_await require_store()->begin_transaction();
}

boost::asio::awaitable<forge::db::object::snapshot> object_handle::begin_read() const {
   co_return co_await require_store()->begin_read();
}

forge::db::object::transaction object_handle::join(forge::db::core::transaction& active) const {
   return require_store()->join(active);
}

std::string blob_handle::name() const {
   if (!state_) {
      return {};
   }
   return state_->name();
}

std::shared_ptr<forge::db::blob::store> blob_handle::require_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store blob handle is empty");
   }
   return state_->require_blobs();
}

boost::asio::awaitable<forge::db::blob::transaction> blob_handle::begin_transaction() const {
   co_return co_await require_store()->begin_transaction();
}

forge::db::blob::transaction blob_handle::join(forge::db::core::transaction& active) const {
   return require_store()->join(active);
}

boost::asio::awaitable<forge::db::blob::ref<forge::db::blob::digest>>
blob_handle::put(std::vector<std::byte> payload) const {
   co_return co_await require_store()->put(std::move(payload));
}

boost::asio::awaitable<forge::db::blob::collect_result>
blob_handle::collect_unreferenced(forge::db::blob::collect_options options) const {
   co_return co_await require_store()->collect_unreferenced(std::move(options));
}

std::string store_handle::name() const {
   if (!state_) {
      return {};
   }
   return state_->name();
}

std::shared_ptr<forge::db::core::driver> store_handle::require_driver() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store handle is empty");
   }
   return state_->require_driver();
}

boost::asio::awaitable<transaction> store_handle::begin_transaction() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store handle is empty");
   }
   co_return co_await state_->begin_transaction();
}

object_handle store_handle::objects() const {
   auto handle = object_handle{state_};
   (void)handle.require_setup_store();
   return handle;
}

blob_handle store_handle::blobs() const {
   auto handle = blob_handle{state_};
   (void)handle.require_store();
   return handle;
}

} // namespace forge::plugins::db::store

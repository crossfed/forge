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
import forge.db.object.exceptions;
import forge.db.object.hooks;
import forge.db.object.snapshot;
import forge.db.object.store;
import forge.db.object.transaction;
import forge.db.revision.store;
import forge.db.revision.transaction;
import forge.db.revision.types;
import forge.plugins.db.store.exceptions;

namespace forge::plugins::db::store {

std::shared_ptr<forge::db::revision::store> store_handle_state::require_revisions() const {
   return {};
}

transaction::transaction(forge::db::core::transaction active, std::string store_name)
   : core_{std::move(active)}, store_name_{std::move(store_name)} {}

transaction::transaction(forge::db::object::transaction active, std::string store_name)
   : object_{std::move(active)}, store_name_{std::move(store_name)} {}

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
   return state_->require_objects();
}

std::shared_ptr<forge::db::object::store> object_handle::require_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store object handle is empty");
   }
   (void)state_->require_driver();
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

boost::asio::awaitable<forge::db::object::transaction>
object_handle::join(forge::db::core::transaction& active) const {
   co_return co_await require_store()->join(active);
}

boost::asio::awaitable<forge::db::object::transaction>
object_handle::join(transaction& active) const {
   if (active.store_name_.empty() || active.store_name_ != name()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                            "db store transaction belongs to another named store");
   }

   auto objects = require_store();
   if (active.object_.has_value()) {
      try {
         co_return co_await objects->join(*active.object_);
      } catch (const forge::db::object::exceptions::invalid_descriptor&) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                               "db store transaction belongs to another object store");
      }
   }
   co_return co_await objects->join(active.db_transaction());
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

std::string revision_handle::name() const {
   if (!state_) {
      return {};
   }
   return state_->name();
}

std::shared_ptr<forge::db::revision::store> revision_handle::require_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store revision handle is empty");
   }
   (void)state_->require_driver();
   auto result = state_->require_revisions();
   if (!result) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable_layer, "db store revision layer is not configured",
                            forge::exceptions::ctx("store", name()));
   }
   return result;
}

void revision_handle::require_own_transaction(const transaction& active) const {
   if (active.store_name_.empty() || active.store_name_ != name()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                            "db store transaction belongs to another named store");
   }
}

boost::asio::awaitable<forge::db::revision::scope>
revision_handle::join(transaction& active) const {
   require_own_transaction(active);
   co_return co_await require_store()->join(active.db_transaction());
}

boost::asio::awaitable<void>
revision_handle::revert(transaction& active,
                        forge::db::revision::revision_id_t expected_head) const {
   require_own_transaction(active);
   co_await require_store()->revert(active.db_transaction(), expected_head);
}

boost::asio::awaitable<forge::db::revision::prune_result>
revision_handle::prune_through(transaction& active,
                               forge::db::revision::revision_id_t inclusive_boundary,
                               forge::db::revision::prune_options options) const {
   require_own_transaction(active);
   co_return co_await require_store()->prune_through(
      active.db_transaction(), inclusive_boundary, options);
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

revision_handle store_handle::revisions() const {
   auto handle = revision_handle{state_};
   (void)handle.require_store();
   return handle;
}

} // namespace forge::plugins::db::store

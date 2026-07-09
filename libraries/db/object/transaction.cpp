module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/system_executor.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <typeindex>
#include <utility>
#include <vector>

module forge.db.object.transaction;

import forge.db.core.exceptions;
import forge.db.object.exceptions;

#include "details/transaction_impl.hxx"

namespace forge::db::object {

transaction::impl::rollback_state::rollback_state(transaction::seal_allocations_fn seal,
                                                  transaction::release_fn release) noexcept
    : seal_allocations{std::move(seal)}, release_writer{std::move(release)} {}

void transaction::impl::rollback_state::release() noexcept {
   if (release_writer) {
      release_writer();
      release_writer = {};
   }
}

void transaction::impl::rollback_state::remember_allocation(forge::ids::object_id type, std::uint64_t next_instance) {
   type.instance = 0;
   auto& existing = allocation_seals[type];
   existing = std::max(existing, next_instance);
}

void transaction::impl::rollback_state::clear_allocations() noexcept {
   allocation_seals.clear();
}

boost::asio::awaitable<void> transaction::impl::rollback_state::after_rollback() {
   auto seals = std::move(allocation_seals);
   allocation_seals.clear();
   try {
      if (seal_allocations && !seals.empty()) {
         co_await seal_allocations(std::move(seals));
      }
   } catch (...) {
      release();
      throw;
   }
   release();
   co_return;
}

transaction::impl::impl(forge::db::core::transaction active_value,
                        forge::db::core::family family_value,
                        transaction::ensure_registered_fn ensure,
                        transaction::allocate_id_fn allocate,
                        transaction::seal_allocations_fn seal,
                        std::vector<std::shared_ptr<interceptor>> interceptors_value,
                        std::vector<std::shared_ptr<observer>> observers_value,
                        transaction::release_fn release) noexcept
    : owned{std::move(active_value)},
      active{&*owned},
      family{std::move(family_value)},
      ensure_registered{std::move(ensure)},
      allocate_id{std::move(allocate)},
      rollback{std::make_shared<rollback_state>(std::move(seal), std::move(release))},
      interceptors{std::move(interceptors_value)},
      observers{std::move(observers_value)},
      owns_commit{true} {}

transaction::impl::impl(forge::db::core::transaction& active_value,
                        forge::db::core::family family_value,
                        transaction::ensure_registered_fn ensure,
                        transaction::allocate_id_fn allocate,
                        transaction::seal_allocations_fn seal,
                        std::vector<std::shared_ptr<interceptor>> interceptors_value,
                        std::vector<std::shared_ptr<observer>> observers_value) noexcept
    : active{&active_value},
      family{std::move(family_value)},
      ensure_registered{std::move(ensure)},
      allocate_id{std::move(allocate)},
      rollback{std::make_shared<rollback_state>(std::move(seal), transaction::release_fn{})},
      interceptors{std::move(interceptors_value)},
      observers{std::move(observers_value)} {}

void transaction::impl::release() noexcept {
   if (rollback) {
      rollback->release();
   }
}

void transaction::impl::remember_allocation(forge::ids::object_id type, std::uint64_t next_instance) {
   if (rollback) {
      rollback->remember_allocation(type, next_instance);
   }
}

boost::asio::awaitable<void> transaction::impl::after_rollback() {
   finalized = true;
   changes.mutations.clear();
   if (rollback) {
      co_await rollback->after_rollback();
   }
   co_return;
}

boost::asio::awaitable<void> transaction::impl::after_commit() {
   finalized = true;
   auto committed_changes = std::move(changes);
   if (rollback) {
      rollback->clear_allocations();
   }
   release();

   if (!committed_changes.empty()) {
      for (const auto& hook : observers) {
         co_await hook->after_commit(committed_changes);
      }
   }
   co_return;
}

transaction::transaction(forge::db::core::transaction&& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         allocate_id_fn allocate,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release)
    : transaction(std::move(active),
                  std::move(family),
                  std::move(ensure),
                  std::move(allocate),
                  seal_allocations_fn{},
                  std::move(interceptors),
                  std::move(observers),
                  std::move(release),
                  boost::asio::system_executor{}) {}

transaction::transaction(forge::db::core::transaction&& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release)
    : transaction(std::move(active),
                  std::move(family),
                  std::move(ensure),
                  allocate_id_fn{},
                  seal_allocations_fn{},
                  std::move(interceptors),
                  std::move(observers),
                  std::move(release),
                  boost::asio::system_executor{}) {}

transaction::transaction(forge::db::core::transaction&& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         allocate_id_fn allocate,
                         seal_allocations_fn seal,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release,
                         boost::asio::any_io_executor)
    : impl_{std::make_shared<impl>(
         std::move(active),
         std::move(family),
         std::move(ensure),
         std::move(allocate),
         std::move(seal),
         std::move(interceptors),
         std::move(observers),
         std::move(release))} {
   auto weak_state = std::weak_ptr<impl>{impl_};
   db_transaction().after_commit([weak_state]() mutable -> boost::asio::awaitable<void> {
      if (auto state = weak_state.lock()) {
         co_await state->after_commit();
      }
      co_return;
   });
   auto rollback_state = impl_->rollback;
   db_transaction().after_rollback([weak_state, rollback_state]() mutable -> boost::asio::awaitable<void> {
      if (auto state = weak_state.lock()) {
         co_await state->after_rollback();
      } else if (rollback_state) {
         co_await rollback_state->after_rollback();
      }
      co_return;
   });
}

transaction::transaction(forge::db::core::transaction&& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release,
                         boost::asio::any_io_executor cleanup_executor)
    : transaction(std::move(active),
                  std::move(family),
                  std::move(ensure),
                  allocate_id_fn{},
                  seal_allocations_fn{},
                  std::move(interceptors),
                  std::move(observers),
                  std::move(release),
                  std::move(cleanup_executor)) {}

transaction::transaction(forge::db::core::transaction& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         allocate_id_fn allocate,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers)
    : transaction(active,
                  std::move(family),
                  std::move(ensure),
                  std::move(allocate),
                  seal_allocations_fn{},
                  std::move(interceptors),
                  std::move(observers)) {}

transaction::transaction(forge::db::core::transaction& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         allocate_id_fn allocate,
                         seal_allocations_fn seal,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers)
    : impl_{std::make_shared<impl>(
         active,
         std::move(family),
         std::move(ensure),
         std::move(allocate),
         std::move(seal),
         std::move(interceptors),
         std::move(observers))} {
   auto state = impl_;
   db_transaction().after_commit([state]() mutable -> boost::asio::awaitable<void> {
      co_await state->after_commit();
      co_return;
   });
   db_transaction().after_rollback([state]() mutable -> boost::asio::awaitable<void> {
      co_await state->after_rollback();
      co_return;
   });
}

transaction::transaction(forge::db::core::transaction& active,
                         forge::db::core::family family,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers)
    : transaction(active,
                  std::move(family),
                  std::move(ensure),
                  allocate_id_fn{},
                  seal_allocations_fn{},
                  std::move(interceptors),
                  std::move(observers)) {}

forge::db::core::transaction& transaction::db_transaction() const {
   return active_transaction();
}

forge::db::core::transaction& transaction::active_transaction() const {
   if (!impl_ || !impl_->active || !impl_->active->active() || impl_->finalized) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object transaction is closed");
   }
   return *impl_->active;
}

change_set& transaction::changes() const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object transaction is closed");
   }
   return impl_->changes;
}

void transaction::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   if (!impl_ || !impl_->ensure_registered) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object transaction is closed");
   }
   impl_->ensure_registered(type, model);
}

boost::asio::awaitable<void> transaction::before_mutation(const object_mutation& mutation) const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object transaction is closed");
   }
   for (const auto& hook : impl_->interceptors) {
      co_await hook->before_mutation(mutation);
   }
   co_return;
}

boost::asio::awaitable<forge::ids::object_id> transaction::allocate_id(forge::ids::object_id type) const {
   if (!impl_ || !impl_->allocate_id) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db object transaction cannot allocate ids");
   }
   auto allocated = co_await impl_->allocate_id(type, active_transaction());
   impl_->remember_allocation(type, allocated.instance + 1U);
   co_return allocated;
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> transaction::get_record(forge::db::core::record_key key) const {
   co_return co_await active_transaction().get(impl_->family, std::move(key));
}

boost::asio::awaitable<void> transaction::put_record(forge::db::core::record_key key, std::vector<std::byte> value) const {
   co_await active_transaction().put(impl_->family, std::move(key), std::move(value));
}

boost::asio::awaitable<void> transaction::erase_record(forge::db::core::record_key key) const {
   co_await active_transaction().erase(impl_->family, std::move(key));
}

boost::asio::awaitable<forge::db::core::record_page> transaction::scan_records(forge::db::core::record_range range, forge::db::core::page_request request) const {
   co_return co_await active_transaction().scan_page(impl_->family, std::move(range), std::move(request));
}

boost::asio::awaitable<void> transaction::commit() {
   if (!impl_ || impl_->finalized) {
      co_return;
   }
   if (!impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined db object transaction does not own commit");
   }
   co_await active_transaction().commit();
   co_return;
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!impl_ || impl_->finalized) {
      co_return;
   }
   if (!impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined db object transaction does not own rollback");
   }
   co_await active_transaction().rollback();
   co_return;
}

} // namespace forge::db::object

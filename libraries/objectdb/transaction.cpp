module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/system_executor.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <utility>
#include <vector>

module forge.objectdb.transaction;

import forge.db.exceptions;
import forge.objectdb.exceptions;

#include "details/transaction_impl.hxx"

namespace forge::objectdb {

transaction::impl::impl(forge::db::transaction active_value,
                        forge::db::family family_value,
                        transaction::ensure_registered_fn ensure,
                        std::vector<std::shared_ptr<interceptor>> interceptors_value,
                        std::vector<std::shared_ptr<observer>> observers_value,
                        transaction::release_fn release) noexcept
    : owned{std::move(active_value)},
      active{&*owned},
      family{std::move(family_value)},
      ensure_registered{std::move(ensure)},
      interceptors{std::move(interceptors_value)},
      observers{std::move(observers_value)},
      release_writer{std::move(release)},
      owns_commit{true} {}

transaction::impl::impl(forge::db::transaction& active_value,
                        forge::db::family family_value,
                        transaction::ensure_registered_fn ensure,
                        std::vector<std::shared_ptr<interceptor>> interceptors_value,
                        std::vector<std::shared_ptr<observer>> observers_value) noexcept
    : active{&active_value},
      family{std::move(family_value)},
      ensure_registered{std::move(ensure)},
      interceptors{std::move(interceptors_value)},
      observers{std::move(observers_value)} {}

void transaction::impl::release() noexcept {
   if (release_writer) {
      release_writer();
      release_writer = {};
   }
}

void transaction::impl::after_rollback() noexcept {
   finalized = true;
   changes.mutations.clear();
   release();
}

boost::asio::awaitable<void> transaction::impl::after_commit() {
   finalized = true;
   auto committed_changes = std::move(changes);
   release();

   if (!committed_changes.empty()) {
      for (const auto& hook : observers) {
         co_await hook->after_commit(committed_changes);
      }
   }
   co_return;
}

transaction::transaction(forge::db::transaction&& active,
                         forge::db::family family,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release)
    : transaction(std::move(active),
                  std::move(family),
                  std::move(ensure),
                  std::move(interceptors),
                  std::move(observers),
                  std::move(release),
                  boost::asio::system_executor{}) {}

transaction::transaction(forge::db::transaction&& active,
                         forge::db::family family,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release,
                         boost::asio::any_io_executor)
    : impl_{std::make_shared<impl>(
         std::move(active),
         std::move(family),
         std::move(ensure),
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
   auto rollback_release = impl_->release_writer;
   db_transaction().after_rollback([weak_state, release = std::move(rollback_release)]() mutable {
      if (auto state = weak_state.lock()) {
         state->after_rollback();
      } else if (release) {
         release();
      }
   });
}

transaction::transaction(forge::db::transaction& active,
                         forge::db::family family,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers)
    : impl_{std::make_shared<impl>(
         active,
         std::move(family),
         std::move(ensure),
         std::move(interceptors),
         std::move(observers))} {
   auto weak_state = std::weak_ptr<impl>{impl_};
   db_transaction().after_commit([weak_state]() mutable -> boost::asio::awaitable<void> {
      if (auto state = weak_state.lock()) {
         co_await state->after_commit();
      }
      co_return;
   });
   db_transaction().after_rollback([weak_state]() mutable {
      if (auto state = weak_state.lock()) {
         state->after_rollback();
      }
   });
}

forge::db::transaction& transaction::db_transaction() const {
   return active_transaction();
}

forge::db::transaction& transaction::active_transaction() const {
   if (!impl_ || !impl_->active || !impl_->active->active() || impl_->finalized) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   return *impl_->active;
}

change_set& transaction::changes() const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   return impl_->changes;
}

void transaction::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   if (!impl_ || !impl_->ensure_registered) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   impl_->ensure_registered(type, model);
}

boost::asio::awaitable<void> transaction::before_mutation(const object_mutation& mutation) const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   for (const auto& hook : impl_->interceptors) {
      co_await hook->before_mutation(mutation);
   }
   co_return;
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> transaction::get_record(record_key key) const {
   co_return co_await active_transaction().get(impl_->family, std::move(key));
}

boost::asio::awaitable<void> transaction::put_record(record_key key, std::vector<std::byte> value) const {
   co_await active_transaction().put(impl_->family, std::move(key), std::move(value));
}

boost::asio::awaitable<void> transaction::erase_record(record_key key) const {
   co_await active_transaction().erase(impl_->family, std::move(key));
}

boost::asio::awaitable<record_page> transaction::scan_records(record_range range, page_request request) const {
   co_return co_await active_transaction().scan_page(impl_->family, std::move(range), std::move(request));
}

boost::asio::awaitable<void> transaction::commit() {
   if (!impl_ || impl_->finalized) {
      co_return;
   }
   if (!impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined objectdb transaction does not own commit");
   }
   co_await active_transaction().commit();
   co_return;
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!impl_ || impl_->finalized) {
      co_return;
   }
   if (!impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined objectdb transaction does not own rollback");
   }
   co_await active_transaction().rollback();
   co_return;
}

} // namespace forge::objectdb

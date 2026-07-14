module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <exception>
#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.db.core.driver;

import forge.db.core.exceptions;

#include "details/transaction_impl.hxx"
#include "details/participant_access_impl.hxx"

namespace forge::db::core {

namespace {

boost::asio::awaitable<void> run_after_rollback_hooks(std::vector<transaction::after_rollback_fn> hooks) {
   for (auto& hook : hooks) {
      if (hook) {
         co_await hook();
      }
   }
   co_return;
}

boost::asio::awaitable<void> rollback_dropped_transaction(std::unique_ptr<session> active,
                                                          std::vector<transaction::after_rollback_fn> hooks,
                                                          std::vector<std::shared_ptr<transaction_participant>> participants) {
   try {
      co_await active->rollback();
   } catch (...) {
   }

   active.reset();

   try {
      co_await run_after_rollback_hooks(std::move(hooks));
   } catch (...) {
   }

   participants.clear();

   co_return;
}

} // namespace

transaction::impl::impl(std::unique_ptr<session> active_value, boost::asio::any_io_executor executor) noexcept
    : active{std::move(active_value)}, cleanup_executor{std::move(executor)} {}

transaction::impl::~impl() {
   rollback_on_drop();
}

boost::asio::awaitable<void> transaction::impl::run_after_rollback() {
   auto hooks = std::move(after_rollback_hooks);
   after_rollback_hooks.clear();
   co_await run_after_rollback_hooks(std::move(hooks));
}

void transaction::impl::rollback_on_drop() noexcept {
   if (!active || closed || committed) {
      active.reset();
      return;
   }

   closed = true;
   auto dropped = std::move(active);
   auto hooks = std::move(after_rollback_hooks);
   auto active_participants = std::move(participants);
   after_rollback_hooks.clear();

   try {
      boost::asio::co_spawn(cleanup_executor,
                            rollback_dropped_transaction(
                               std::move(dropped), std::move(hooks), std::move(active_participants)),
                            boost::asio::detached);
   } catch (...) {
      dropped.reset();
   }
}

transaction::transaction(std::unique_ptr<session> active, boost::asio::any_io_executor cleanup_executor)
    : impl_{std::make_shared<impl>(std::move(active), std::move(cleanup_executor))} {
   if (!impl_->active) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db transaction session is null");
   }
   const auto caps = impl_->active->capabilities();
   if (!caps.writes) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support writes");
   }
}

transaction::~transaction() = default;
transaction::transaction(transaction&&) noexcept = default;
transaction& transaction::operator=(transaction&&) noexcept = default;

bool transaction::active() const noexcept {
   return impl_ && impl_->active && impl_->current != impl::phase::closed;
}

capabilities transaction::capabilities() const noexcept {
   return active() ? impl_->active->capabilities() : forge::db::core::capabilities{};
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> transaction::get(family column_family, record_key key) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }
   co_return co_await impl_->active->get(std::move(column_family), std::move(key));
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
transaction::get_for_update(family column_family, record_key key) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }
   if (!impl_->active->capabilities().record_locks) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db transaction does not support record locks");
   }
   co_return co_await impl_->active->get_for_update(std::move(column_family), std::move(key));
}

boost::asio::awaitable<void> transaction::put(family column_family, record_key key, std::vector<std::byte> value) {
   co_await mutate(std::move(column_family), std::move(key), std::move(value));
}

boost::asio::awaitable<void> transaction::erase(family column_family, record_key key) {
   co_await mutate(std::move(column_family), std::move(key), std::nullopt);
}

boost::asio::awaitable<void>
transaction::mutate(family column_family, record_key key, std::optional<std::vector<std::byte>> after) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }

   const auto kind = after.has_value() ? mutation_kind::put : mutation_kind::erase;
   auto policy = mutation_policy::reversible;
   auto capture = std::vector<std::shared_ptr<transaction_participant>>{};
   for (const auto& participant : impl_->participants) {
      const auto classified = participant->classify(column_family, key, kind);
      if (classified == mutation_policy::forbidden) {
         policy = mutation_policy::forbidden;
      } else if (classified == mutation_policy::excluded &&
                 policy != mutation_policy::forbidden) {
         policy = mutation_policy::excluded;
      }
      if (participant->captures_mutations()) {
         capture.push_back(participant);
      }
   }

   if (policy == mutation_policy::forbidden && !capture.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::mutation_forbidden, "db record mutation is forbidden in this transaction");
   }

   const auto mutation = record_mutation{
      .kind = kind,
      .column_family = column_family,
      .key = key,
      .before = std::nullopt,
      .after = after,
   };

   if (policy == mutation_policy::reversible && !capture.empty()) {
      auto captured = mutation;
      captured.before = co_await impl_->active->get(column_family, key);
      try {
         for (const auto& participant : capture) {
            auto participant_mutation = captured;
            if (auto token = participant->retention_token(participant_mutation)) {
               for (const auto& provider : impl_->participants) {
                  auto guard = provider->make_retention_guard(participant_mutation, *token);
                  if (!guard) {
                     continue;
                  }
                  if (participant_mutation.retention_guard) {
                     FORGE_THROW_EXCEPTION(exceptions::participant_conflict,
                                           "multiple db participants produced retention guards");
                  }
                  participant_mutation.retention_guard = std::move(guard);
               }
            }
            co_await participant->prepare_mutation(participant_mutation);
         }
      } catch (...) {
         for (auto iterator = capture.rbegin(); iterator != capture.rend(); ++iterator) {
            (*iterator)->discard_mutation();
         }
         throw;
      }
   } else {
      capture.clear();
   }

   try {
      if (after.has_value()) {
         co_await impl_->active->put(std::move(column_family), std::move(key), std::move(*after));
      } else {
         co_await impl_->active->erase(std::move(column_family), std::move(key));
      }
   } catch (...) {
      for (auto iterator = capture.rbegin(); iterator != capture.rend(); ++iterator) {
         (*iterator)->discard_mutation();
      }
      throw;
   }

   for (const auto& participant : capture) {
      participant->publish_mutation();
   }
   impl_->mutation_started = true;
}

boost::asio::awaitable<record_page> transaction::scan_page(family column_family,
                                                           record_range range,
                                                           page_request request) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }
   validate_page_request(request);
   co_return co_await impl_->active->scan_page(std::move(column_family), std::move(range), std::move(request));
}

void transaction::attach_participant(std::shared_ptr<transaction_participant> participant) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (!participant) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db transaction participant is null");
   }
   if (impl_->current != impl::phase::active || impl_->mutation_started || !impl_->savepoints.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict,
                            "db transaction participants must attach before mutations or savepoints");
   }
   for (const auto& existing : impl_->participants) {
      if (existing == participant || existing->name() == participant->name()) {
         FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction participant is already attached");
      }

      for (const auto& claimed : participant->exclusive_families()) {
         const auto existing_claims = existing->exclusive_families();
         const auto overlap = std::find_if(
            existing_claims.begin(), existing_claims.end(),
            [&claimed](const family& value) { return value.name == claimed.name; });
         if (overlap != existing_claims.end()) {
            FORGE_THROW_EXCEPTION(
               exceptions::participant_conflict,
               "db transaction family is already claimed by another participant",
               forge::exceptions::ctx("family", claimed.name),
               forge::exceptions::ctx("participant", participant->name()),
               forge::exceptions::ctx("existing-participant", existing->name()));
         }
      }
   }
   impl_->participants.push_back(std::move(participant));
}

bool transaction::has_participant(std::string_view name) const noexcept {
   if (!active()) {
      return false;
   }
   return std::any_of(
      impl_->participants.begin(),
      impl_->participants.end(),
      [name](const auto& participant) { return participant && participant->name() == name; });
}

bool transaction::claims_family(const family& column_family) const noexcept {
   if (!active()) {
      return false;
   }
   return std::any_of(
      impl_->participants.begin(),
      impl_->participants.end(),
      [&column_family](const auto& participant) {
         if (!participant) {
            return false;
         }
         const auto claims = participant->exclusive_families();
         return std::any_of(
            claims.begin(),
            claims.end(),
            [&column_family](const family& claimed) { return claimed.name == column_family.name; });
      });
}

boost::asio::awaitable<savepoint_id_t> transaction::create_savepoint() {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }
   if (!impl_->active->capabilities().savepoints) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db transaction does not support savepoints");
   }
   if (impl_->next_savepoint == std::numeric_limits<savepoint_id_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::savepoint_overflow, "db transaction savepoint id is exhausted");
   }

   const auto savepoint = impl_->next_savepoint;
   try {
      for (const auto& participant : impl_->participants) {
         co_await participant->prepare_savepoint(savepoint);
      }
      co_await impl_->active->create_savepoint();
   } catch (...) {
      for (auto iterator = impl_->participants.rbegin(); iterator != impl_->participants.rend(); ++iterator) {
         (*iterator)->discard_savepoint(savepoint);
      }
      throw;
   }

   for (const auto& participant : impl_->participants) {
      participant->publish_savepoint(savepoint);
   }
   impl_->savepoints.push_back(savepoint);
   ++impl_->next_savepoint;
   co_return savepoint;
}

boost::asio::awaitable<void> transaction::rollback_to_savepoint(savepoint_id_t savepoint) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }
   if (impl_->savepoints.empty() || impl_->savepoints.back() != savepoint) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_savepoint, "db savepoint is unknown, stale, or not the top savepoint");
   }

   co_await impl_->active->rollback_to_savepoint();
   impl_->savepoints.pop_back();

   auto access = participant_access_impl{*impl_->active};
   auto error = std::exception_ptr{};
   for (auto iterator = impl_->participants.rbegin(); iterator != impl_->participants.rend(); ++iterator) {
      try {
         co_await (*iterator)->rollback_to_savepoint(savepoint, access);
      } catch (...) {
         if (!error) {
            error = std::current_exception();
         }
      }
   }
   if (error) {
      impl_->current = impl::phase::rollback_only;
      std::rethrow_exception(error);
   }
}

boost::asio::awaitable<void> transaction::release_savepoint(savepoint_id_t savepoint) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }
   if (impl_->current != impl::phase::active) {
      FORGE_THROW_EXCEPTION(exceptions::participant_conflict, "db transaction is preparing or prepared");
   }
   if (impl_->savepoints.empty() || impl_->savepoints.back() != savepoint) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_savepoint, "db savepoint is unknown, stale, or not the top savepoint");
   }

   co_await impl_->active->release_savepoint();
   impl_->savepoints.pop_back();

   auto access = participant_access_impl{*impl_->active};
   auto error = std::exception_ptr{};
   for (auto iterator = impl_->participants.rbegin(); iterator != impl_->participants.rend(); ++iterator) {
      try {
         co_await (*iterator)->release_savepoint(savepoint, access);
      } catch (...) {
         if (!error) {
            error = std::current_exception();
         }
      }
   }
   if (error) {
      impl_->current = impl::phase::rollback_only;
      std::rethrow_exception(error);
   }
}

void transaction::after_commit(after_commit_fn hook) {
   if (hook) {
      impl_->after_commit_hooks.push_back(std::move(hook));
   }
}

void transaction::after_rollback(after_rollback_fn hook) {
   if (hook) {
      impl_->after_rollback_hooks.push_back(std::move(hook));
   }
}

boost::asio::awaitable<void> transaction::commit() {
   if (!active()) {
      co_return;
   }

   if (impl_->current == impl::phase::rollback_only) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_rollback_only, "db transaction is rollback-only");
   }

   if (impl_->current == impl::phase::active) {
      impl_->current = impl::phase::preparing;
      auto access = participant_access_impl{*impl_->active};
      try {
         for (const auto& participant : impl_->participants) {
            co_await participant->prepare_commit(access);
         }
      } catch (...) {
         impl_->current = impl::phase::rollback_only;
         throw;
      }
      impl_->current = impl::phase::prepared;
   }

   co_await impl_->active->commit();

   auto active_session = std::move(impl_->active);
   auto after_commit_hooks = std::move(impl_->after_commit_hooks);
   auto active_participants = std::move(impl_->participants);
   impl_->after_rollback_hooks.clear();
   impl_->savepoints.clear();
   impl_->current = impl::phase::closed;
   impl_->closed = true;
   impl_->committed = true;

   active_session.reset();
   for (auto& hook : after_commit_hooks) {
      if (hook) {
         co_await hook();
      }
   }
   active_participants.clear();
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!active()) {
      co_return;
   }

   auto active_session = std::move(impl_->active);
   auto hooks = std::move(impl_->after_rollback_hooks);
   auto active_participants = std::move(impl_->participants);
   impl_->after_commit_hooks.clear();
   impl_->after_rollback_hooks.clear();
   impl_->closed = true;
   impl_->current = impl::phase::closed;
   impl_->savepoints.clear();

   auto error = std::exception_ptr{};
   try {
      co_await active_session->rollback();
   } catch (...) {
      error = std::current_exception();
   }
   active_session.reset();
   for (auto& hook : hooks) {
      if (hook) {
         try {
            co_await hook();
         } catch (...) {
            if (!error) {
               error = std::current_exception();
            }
         }
      }
   }
   active_participants.clear();
   if (error) {
      std::rethrow_exception(error);
   }
}

} // namespace forge::db::core

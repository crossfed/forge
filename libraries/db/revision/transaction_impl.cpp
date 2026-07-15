module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.db.revision.store;

import forge.db.core.record;
import forge.db.object.system;
import forge.db.revision.exceptions;
import forge.raw.raw;

#include "details/codec.hxx"
#include "details/transaction_impl.hxx"

namespace forge::db::revision::detail {

transaction_impl::transaction_impl(forge::db::core::family family, state initial)
    : family_{std::move(family)},
      state_{std::move(initial)},
      candidate_{state_.next_revision},
      prewrite_locks_{{.column_family = family_, .key = state_key()}} {}

void transaction_impl::reset_state(state current) {
   if (!deltas_.empty() || pending_mutation_ || pending_savepoint_ || !savepoints_.empty() || prepared_) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state,
                            "db revision state cannot change after mutation capture");
   }
   state_ = std::move(current);
   candidate_ = state_.next_revision;
   valid_ = true;
}

void transaction_impl::invalidate() noexcept {
   valid_ = false;
}

revision_id_t transaction_impl::id() const noexcept {
   return candidate_;
}

std::string_view transaction_impl::name() const noexcept {
   return "forge.db.revision";
}

bool transaction_impl::captures_mutations() const noexcept {
   return true;
}

std::span<const forge::db::core::record_lock_claim>
transaction_impl::prewrite_locks() const noexcept {
   return prewrite_locks_;
}

std::optional<std::vector<std::byte>>
transaction_impl::retention_token(const forge::db::core::record_mutation&) const {
   if (!valid_) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision participant is invalid");
   }
   auto token = std::vector<std::byte>{};
   token.reserve(sizeof(candidate_));
   for (auto shift = 56; shift >= 0; shift -= 8) {
      token.push_back(static_cast<std::byte>((candidate_ >> static_cast<unsigned>(shift)) & 0xffU));
   }
   return token;
}

boost::asio::awaitable<void>
transaction_impl::prepare_mutation(const forge::db::core::record_mutation& mutation) {
   if (!valid_) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision participant is invalid");
   }
   if (pending_mutation_) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision mutation preparation overlaps");
   }
   pending_mutation_ = mutation;
   co_return;
}

void transaction_impl::publish_mutation() noexcept {
   if (!pending_mutation_) {
      return;
   }

   auto mutation = std::move(*pending_mutation_);
   pending_mutation_.reset();
   auto location = address{.family = mutation.column_family.name, .key = mutation.key.bytes()};
   const auto found = index_.find(location);
   if (found == index_.end()) {
      index_.emplace(location, deltas_.size());
      deltas_.push_back(captured_delta{
         .location = std::move(location),
         .before = std::move(mutation.before),
         .after = std::move(mutation.after),
         .retention_guard = std::move(mutation.retention_guard),
      });
      return;
   }

   const auto index = found->second;
   for (auto& frame : savepoints_) {
      if (index < frame.delta_count) {
         frame.previous_after.try_emplace(index, deltas_[index].after);
         frame.previous_guards.try_emplace(index, deltas_[index].retention_guard);
      }
   }
   deltas_[index].after = std::move(mutation.after);
   deltas_[index].retention_guard = std::move(mutation.retention_guard);
}

void transaction_impl::discard_mutation() noexcept {
   pending_mutation_.reset();
}

boost::asio::awaitable<void>
transaction_impl::prepare_savepoint(forge::db::core::savepoint_id_t id) {
   if (!valid_) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision participant is invalid");
   }
   pending_savepoint_ = savepoint_frame{.id = id, .delta_count = deltas_.size()};
   co_return;
}

void transaction_impl::publish_savepoint(forge::db::core::savepoint_id_t id) noexcept {
   if (pending_savepoint_ && pending_savepoint_->id == id) {
      savepoints_.push_back(std::move(*pending_savepoint_));
   }
   pending_savepoint_.reset();
}

void transaction_impl::discard_savepoint(forge::db::core::savepoint_id_t id) noexcept {
   if (pending_savepoint_ && pending_savepoint_->id == id) {
      pending_savepoint_.reset();
   }
}

boost::asio::awaitable<void>
transaction_impl::rollback_to_savepoint(forge::db::core::savepoint_id_t id,
                                        forge::db::core::participant_access&) {
   if (savepoints_.empty() || savepoints_.back().id != id) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision savepoint frame is inconsistent");
   }
   auto frame = std::move(savepoints_.back());
   savepoints_.pop_back();
   deltas_.resize(frame.delta_count);
   for (auto& [index, previous] : frame.previous_after) {
      deltas_.at(index).after = std::move(previous);
   }
   for (auto& [index, previous] : frame.previous_guards) {
      deltas_.at(index).retention_guard = std::move(previous);
   }
   rebuild_index();
   co_return;
}

boost::asio::awaitable<void>
transaction_impl::release_savepoint(forge::db::core::savepoint_id_t id,
                                    forge::db::core::participant_access&) {
   if (savepoints_.empty() || savepoints_.back().id != id) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision savepoint frame is inconsistent");
   }
   savepoints_.pop_back();
   co_return;
}

void transaction_impl::rebuild_index() {
   index_.clear();
   for (auto index = std::size_t{0}; index < deltas_.size(); ++index) {
      index_.emplace(deltas_[index].location, index);
   }
}

boost::asio::awaitable<void>
transaction_impl::prepare_commit(forge::db::core::participant_access& access) {
   if (!valid_) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision participant is invalid");
   }
   if (prepared_) {
      co_return;
   }
   if (candidate_ == 0 || candidate_ == std::numeric_limits<revision_id_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::revision_overflow, "db revision id sequence is exhausted");
   }

   auto effective = std::vector<const captured_delta*>{};
   effective.reserve(deltas_.size());
   for (const auto& value : deltas_) {
      if (value.before != value.after) {
         effective.push_back(&value);
      }
   }
   if (effective.size() > std::numeric_limits<std::uint64_t>::max() - state_.next_delta) {
      FORGE_THROW_EXCEPTION(exceptions::revision_overflow, "db revision delta id sequence is exhausted");
   }

   const auto first_delta = effective.empty() ? std::uint64_t{0} : state_.next_delta;
   auto ordinal = std::uint64_t{0};
   for (const auto* captured : effective) {
      auto value = delta{};
      value.id = delta::id_t{state_.next_delta + ordinal};
      value.revision = candidate_;
      value.ordinal = ordinal;
      value.family = captured->location.family;
      value.key = captured->location.key;
      value.before = captured->before;
      if (captured->retention_guard) {
         value.retention_guard = retention_guard_address{
            .family = captured->retention_guard->column_family.name,
            .key = captured->retention_guard->key.bytes(),
         };
         co_await access.put(
            captured->retention_guard->column_family,
            captured->retention_guard->key,
            std::vector<std::byte>{static_cast<std::byte>(1U)});
      }
      co_await access.put(family_, delta_key(value.id.instance), encode(value));
      ++ordinal;
   }

   auto revision_entry = entry{};
   revision_entry.id = entry::id_t{candidate_};
   revision_entry.parent = state_.head;
   revision_entry.first_delta = first_delta;
   revision_entry.delta_count = ordinal;
   co_await access.put(family_, entry_key(candidate_), encode(revision_entry));

   if (!state_.head && state_.oldest_retained >= state_.next_revision) {
      state_.oldest_retained = candidate_;
   }
   state_.head = candidate_;
   state_.next_revision = candidate_ + 1U;
   state_.next_delta += ordinal;
   co_await access.put(family_, state_key(), encode(state_));
   prepared_ = true;
}

} // namespace forge::db::revision::detail

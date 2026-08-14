module;

#include <boost/asio/awaitable.hpp>

#include <forge/exceptions/macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.dht.record_store;

import forge.asio.gate;
import forge.net.p2p.exceptions;

#include "details/dht_record_store_impl.hxx"

namespace forge::net::p2p {
namespace {

[[noreturn]] void throw_record_rejected(bool incoming, std::string message) {
   if (incoming) {
      FORGE_THROW_EXCEPTION(exceptions::record_rejected, std::move(message));
   }
   FORGE_THROW_EXCEPTION(exceptions::internal, "Persisted DHT value record failed validation",
                         forge::exceptions::ctx("reason", std::move(message)));
}

} // namespace

std::size_t dht::record_store::impl::value_bytes(const dht::record_store::value_record& value) const {
   const auto publisher_bytes = value.record.publisher ? value.record.publisher->value.size() : 0U;
   return value.record.key_value.bytes.size() + value.record.value.size() + value.record.time_received.size() +
          publisher_bytes;
}

const dht::value_policy& dht::record_store::impl::prepare_value(dht::record_store::value_record& value,
                                                                std::chrono::system_clock::time_point now,
                                                                bool incoming) const {
   if (!profile_.capabilities.values) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "DHT profile does not enable value records");
   }
   if (value.record.key_value.bytes.empty()) {
      throw_record_rejected(incoming, "DHT value record key must not be empty");
   }
   if (value.expires_at == std::chrono::system_clock::time_point{} || value.expires_at <= now) {
      throw_record_rejected(incoming, "DHT value record expiry must be in the future");
   }
   const auto* policy = value_policy_for(profile_, value.record.key_value.bytes);
   if (!policy) {
      throw_record_rejected(incoming, "DHT value key has no configured policy");
   }
   if (value.record.value.size() > profile_.limits.max_record_size) {
      throw_record_rejected(incoming, "DHT wire value payload exceeds profile limit");
   }
   if (value_bytes(value) > options_.max_record_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT value record byte limit exceeded");
   }
   try {
      policy->validate(value.record,
                       dht::value_validation_context{
                           .now = now, .public_keys = options_.public_keys ? &options_.public_keys : nullptr});
      value.expires_at =
          policy->expiry(value.record, dht::value_expiry_context{.now = now, .supplied_expires_at = value.expires_at});
   } catch (const forge::exceptions::base& error) {
      if (exceptions::is(error, exceptions::code::record_rejected)) {
         throw_record_rejected(incoming, error.what());
      }
      throw;
   }
   if (value.expires_at == std::chrono::system_clock::time_point{} || value.expires_at <= now) {
      throw_record_rejected(incoming, "DHT value policy expiry must be in the future");
   }
   return *policy;
}

void dht::record_store::impl::ensure_value_capacity_locked(const dht::record_store::value_record& value) const {
   const auto current = values_.find(value.record.key_value.bytes);
   if (current == values_.end() && values_.size() >= options_.max_values) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT value capacity reached");
   }
   const auto old_bytes = current == values_.end() ? 0U : value_bytes(current->second);
   const auto new_bytes = value_bytes(value);
   if (exceeds(total_bytes_, old_bytes, new_bytes, options_.max_total_bytes)) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT record store total byte capacity reached");
   }
}

void dht::record_store::impl::publish_value_locked(dht::record_store::value_record value) {
   const auto key = value.record.key_value.bytes;
   if (const auto current = values_.find(key); current != values_.end()) {
      total_bytes_ -= value_bytes(current->second);
   }
   total_bytes_ += value_bytes(value);
   values_.insert_or_assign(key, std::move(value));
}

void dht::record_store::impl::erase_value_locked(const dht::key& key) {
   const auto current = values_.find(key.bytes);
   if (current == values_.end()) {
      return;
   }
   total_bytes_ -= value_bytes(current->second);
   values_.erase(current);
}

boost::asio::awaitable<dht::record_store::put_result>
dht::record_store::impl::async_put(dht::record_store::value_record incoming,
                                   std::chrono::system_clock::time_point now) {
   auto result = co_await async_put_impl(std::move(incoming), now, false);
   if (!result) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "DHT value validation unexpectedly discarded a direct put");
   }
   co_return std::move(*result);
}

boost::asio::awaitable<std::optional<dht::record_store::put_result>>
dht::record_store::impl::async_put_received(dht::record_store::value_record incoming,
                                            std::chrono::system_clock::time_point now) {
   co_return co_await async_put_impl(std::move(incoming), now, true);
}

boost::asio::awaitable<std::optional<dht::record_store::put_result>>
dht::record_store::impl::async_put_impl(dht::record_store::value_record incoming,
                                        std::chrono::system_clock::time_point now, bool discard_rejected) {
   auto admission = admit_operation();
   auto ticket = co_await persistence_gate_.acquire();
   const auto* policy = static_cast<const dht::value_policy*>(nullptr);
   try {
      policy = &prepare_value(incoming, now, true);
   } catch (const exceptions::record_rejected&) {
      if (discard_rejected) {
         co_return std::nullopt;
      }
      throw;
   }

   auto current = std::optional<dht::record_store::value_record>{};
   {
      auto lock = std::scoped_lock{mutex_};
      const auto found = values_.find(incoming.record.key_value.bytes);
      if (found != values_.end() && !expired(found->second.expires_at, now)) {
         current = found->second;
      }
   }
   if (current) {
      static_cast<void>(prepare_value(*current, now, false));
      const auto candidates = std::array<dht::record, 2>{incoming.record, current->record};
      const auto selected = policy->select(candidates);
      if (selected >= candidates.size()) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT value selector returned an invalid index");
      }
      if (selected != 0) {
         co_return dht::record_store::put_result{
             .selected = std::move(*current),
             .outcome = dht::record_store::put_outcome::existing_preferred,
         };
      }
   }

   {
      auto lock = std::scoped_lock{mutex_};
      ensure_open_locked();
      ensure_value_capacity_locked(incoming);
   }
   auto batch = dht::record_store::mutation_batch{};
   batch.value_upserts.push_back(incoming);
   auto result = dht::record_store::apply_result{};
   try {
      result = co_await persistence_->async_apply(std::move(batch));
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }
   {
      auto lock = std::scoped_lock{mutex_};
      publish_value_locked(incoming);
      apply_durability_result_locked(result);
   }
   if (!result.durability_confirmed) {
      throw_durability_uncertain(result);
   }
   co_return dht::record_store::put_result{
       .selected = std::move(incoming),
       .outcome = dht::record_store::put_outcome::incoming_stored,
   };
}

std::optional<dht::record_store::value_record>
dht::record_store::impl::find_value(const dht::key& key, std::chrono::system_clock::time_point now) const {
   auto lock = std::scoped_lock{mutex_};
   const auto current = values_.find(key.bytes);
   if (current == values_.end() || expired(current->second.expires_at, now)) {
      return std::nullopt;
   }
   return current->second;
}

} // namespace forge::net::p2p

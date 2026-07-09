module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

module forge.db.object.store;

import forge.db.core.exceptions;
import forge.db.object.exceptions;

#include "details/store_impl.hxx"

namespace forge::db::object {

namespace {

constexpr auto sequence_record_kind = std::uint8_t{0x02};
constexpr auto object_record_kind = std::uint8_t{0x10};
constexpr auto sequence_value_size = std::size_t{8};

void append_byte(std::vector<std::byte>& out, std::uint8_t value) {
   out.push_back(static_cast<std::byte>(value));
}

void append_be16(std::vector<std::byte>& out, std::uint16_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

void append_be64(std::vector<std::byte>& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      append_byte(out, static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

forge::db::core::record_key sequence_record_key(forge::ids::object_id type) {
   auto bytes = std::vector<std::byte>{};
   bytes.reserve(4);
   append_byte(bytes, sequence_record_kind);
   append_byte(bytes, type.space);
   append_be16(bytes, type.type);
   return forge::db::core::record_key{std::move(bytes)};
}

forge::db::core::record_key object_record_key(forge::ids::object_id type, std::uint64_t instance) {
   auto bytes = std::vector<std::byte>{};
   bytes.reserve(12);
   append_byte(bytes, object_record_kind);
   append_byte(bytes, type.space);
   append_be16(bytes, type.type);
   append_be64(bytes, instance);
   return forge::db::core::record_key{std::move(bytes)};
}

std::vector<std::byte> encode_next_instance(std::uint64_t value) {
   auto out = std::vector<std::byte>{};
   out.reserve(sequence_value_size);
   append_be64(out, value);
   return out;
}

std::uint64_t decode_next_instance(const std::vector<std::byte>& bytes) {
   if (bytes.size() != sequence_value_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object id sequence record has invalid size");
   }

   auto value = std::uint64_t{0};
   for (auto byte : bytes) {
      value <<= 8U;
      value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte));
   }
   return value;
}

struct runtime_entry {
   std::weak_ptr<forge::db::core::driver> driver;
   std::string family;
   std::weak_ptr<runtime_state> runtime;
};

std::vector<runtime_entry>& runtime_registry() {
   static auto entries = std::vector<runtime_entry>{};
   return entries;
}

std::mutex& runtime_registry_mutex() {
   static auto mutex = std::mutex{};
   return mutex;
}

bool same_owner(const std::shared_ptr<forge::db::core::driver>& left,
                const std::shared_ptr<forge::db::core::driver>& right) noexcept {
   const auto less = std::owner_less<std::shared_ptr<forge::db::core::driver>>{};
   return !less(left, right) && !less(right, left);
}

std::shared_ptr<runtime_state> acquire_runtime_state(
   const std::shared_ptr<forge::db::core::driver>& driver,
   const forge::db::core::family& family) {
   auto guard = std::scoped_lock{runtime_registry_mutex()};
   auto& entries = runtime_registry();

   entries.erase(std::remove_if(entries.begin(),
                                entries.end(),
                                [](const runtime_entry& entry) {
                                   return entry.driver.expired() || entry.runtime.expired();
                                }),
                 entries.end());

   for (const auto& entry : entries) {
      auto existing_driver = entry.driver.lock();
      if (existing_driver && entry.family == family.name && same_owner(existing_driver, driver)) {
         if (auto runtime = entry.runtime.lock()) {
            return runtime;
         }
      }
   }

   auto runtime = std::make_shared<runtime_state>();
   entries.push_back(runtime_entry{.driver = driver, .family = family.name, .runtime = runtime});
   return runtime;
}

} // namespace

store::impl::impl(std::shared_ptr<forge::db::core::driver> driver_value,
                  store::config config_value,
                  store::options options_value)
    : driver{std::move(driver_value)},
      config{std::move(config_value)},
      settings{options_value} {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   if (config.family.name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object family is empty");
   }
   runtime = acquire_runtime_state(driver, config.family);
}

boost::asio::awaitable<forge::db::core::transaction> store::impl::open_write_transaction() const {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   co_return co_await driver->begin_transaction();
}

boost::asio::awaitable<forge::db::core::snapshot> store::impl::open_read_snapshot() const {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   co_return co_await driver->begin_read();
}

boost::asio::awaitable<forge::ids::object_id> store::impl::allocate_id(forge::ids::object_id type,
                                                                       forge::db::core::transaction& active) {
   const auto ticket = co_await runtime->allocator_gate->acquire();
   auto allocated = type;

   const auto key = sequence_record_key(type);
   auto cursor = runtime->next_instances.find(type);
   if (cursor == runtime->next_instances.end()) {
      const auto existing = co_await active.get(config.family, key);
      const auto next = existing.has_value() ? decode_next_instance(*existing) : std::uint64_t{0};
      cursor = runtime->next_instances.emplace(type, next).first;
   }

   auto next = cursor->second;
   if (next == std::numeric_limits<std::uint64_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object id sequence is exhausted");
   }

   while ((co_await active.get(config.family, object_record_key(type, next))).has_value()) {
      if (next == std::numeric_limits<std::uint64_t>::max()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object id sequence is exhausted");
      }
      ++next;
   }
   if (next == std::numeric_limits<std::uint64_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object id sequence is exhausted");
   }

   allocated.instance = next;
   cursor->second = next + 1U;
   co_await active.put(config.family, key, encode_next_instance(cursor->second));
   co_return allocated;
}

boost::asio::awaitable<void> store::impl::seal_allocations(transaction::allocation_seal_map seals) {
   if (seals.empty()) {
      co_return;
   }

   const auto ticket = co_await runtime->allocator_gate->acquire();
   auto active = co_await open_write_transaction();
   auto error = std::exception_ptr{};

   try {
      for (auto& [type, consumed_next] : seals) {
         auto sealed_type = type;
         sealed_type.instance = 0;
         const auto key = sequence_record_key(sealed_type);
         const auto existing = co_await active.get(config.family, key);
         const auto stored_next = existing.has_value() ? decode_next_instance(*existing) : std::uint64_t{0};
         const auto sealed_next = std::max(stored_next, consumed_next);
         if (sealed_next != stored_next) {
            co_await active.put(config.family, key, encode_next_instance(sealed_next));
         }

         auto cursor = runtime->next_instances.find(sealed_type);
         if (cursor == runtime->next_instances.end()) {
            runtime->next_instances.emplace(sealed_type, sealed_next);
         } else {
            cursor->second = std::max(cursor->second, sealed_next);
         }
      }

      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }

   if (error) {
      try {
         co_await active.rollback();
      } catch (...) {
      }
      std::rethrow_exception(error);
   }
   co_return;
}

void store::impl::register_object_type(forge::ids::object_id type, std::type_index model) {
   if (registered.contains(type)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object type is already registered");
   }
   registered.emplace(type, model);
}

void store::impl::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   const auto found = registered.find(type);
   if (found == registered.end() || found->second != model) {
      FORGE_THROW_EXCEPTION(exceptions::unregistered_object, "db object type is not registered");
   }
}

} // namespace forge::db::object

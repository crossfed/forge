module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
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

} // namespace

store::impl::impl(std::shared_ptr<forge::db::core::driver> driver_value,
                  store::config config_value,
                  store::options options_value)
    : driver{std::move(driver_value)},
      config{std::move(config_value)},
      settings{options_value},
      write_gate{std::make_shared<detail::write_gate>()},
      allocator_gate{std::make_shared<detail::write_gate>()} {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   if (config.family.name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object family is empty");
   }
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

boost::asio::awaitable<forge::ids::object_id> store::impl::allocate_id(forge::ids::object_id type) const {
   const auto ticket = co_await allocator_gate->acquire();
   auto active = co_await open_write_transaction();
   auto error = std::exception_ptr{};
   auto allocated = type;

   try {
      const auto key = sequence_record_key(type);
      const auto existing = co_await active.get(config.family, key);
      auto next = existing.has_value() ? decode_next_instance(*existing) : std::uint64_t{0};
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
      ++next;
      co_await active.put(config.family, key, encode_next_instance(next));
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

   co_return allocated;
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

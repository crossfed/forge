#pragma once

namespace forge::db::object::detail::record_key {

enum class entry_kind : std::uint8_t {
   system_record = 0x00,
   sequence_record = 0x02,
   object_record = 0x10,
   ordered_unique_index = 0x20,
   ordered_non_unique_index = 0x21,
   ranked_index_root = 0x30,
   ranked_index_level = 0x31,
   ranked_index_coordinator = 0x32,
};

inline void append_byte(std::vector<std::byte>& out, std::uint8_t value) {
   out.push_back(static_cast<std::byte>(value));
}

inline void append_be16(std::vector<std::byte>& out, std::uint16_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

inline void append_be32(std::vector<std::byte>& out, std::uint32_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

inline void append_be64(std::vector<std::byte>& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      append_byte(out, static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

inline void append_application_prefix(std::vector<std::byte>& out, entry_kind kind,
                                      forge::ids::object_id type) {
   append_byte(out, static_cast<std::uint8_t>(kind));
   append_byte(out, type.space);
   append_be16(out, type.type);
}

[[nodiscard]] inline forge::db::core::record_key sequence(forge::ids::object_id type) {
   auto bytes = std::vector<std::byte>{};
   bytes.reserve(4U);
   append_application_prefix(bytes, entry_kind::sequence_record, type);
   return forge::db::core::record_key{std::move(bytes)};
}

[[nodiscard]] inline forge::db::core::record_key object(forge::ids::object_id id) {
   auto bytes = std::vector<std::byte>{};
   if (id.space == forge::db::object::system_space) {
      bytes.reserve(11U);
      append_byte(bytes, static_cast<std::uint8_t>(entry_kind::system_record));
      append_be16(bytes, id.type);
   } else {
      bytes.reserve(12U);
      append_application_prefix(bytes, entry_kind::object_record, id);
   }
   append_be64(bytes, id.instance);
   return forge::db::core::record_key{std::move(bytes)};
}

[[nodiscard]] inline std::vector<std::byte> object_prefix(forge::ids::object_id type) {
   auto bytes = std::vector<std::byte>{};
   if (type.space == forge::db::object::system_space) {
      bytes.reserve(3U);
      append_byte(bytes, static_cast<std::uint8_t>(entry_kind::system_record));
      append_be16(bytes, type.type);
   } else {
      bytes.reserve(4U);
      append_application_prefix(bytes, entry_kind::object_record, type);
   }
   return bytes;
}

[[nodiscard]] inline forge::db::core::record_key ranked_root(forge::ids::object_id type,
                                                              std::uint32_t ordinal) {
   auto bytes = std::vector<std::byte>{};
   bytes.reserve(8U);
   append_application_prefix(bytes, entry_kind::ranked_index_root, type);
   append_be32(bytes, ordinal);
   return forge::db::core::record_key{std::move(bytes)};
}

[[nodiscard]] inline std::vector<std::byte> ranked_level_prefix(forge::ids::object_id type,
                                                                 std::uint32_t ordinal,
                                                                 std::uint8_t level) {
   auto bytes = std::vector<std::byte>{};
   bytes.reserve(9U);
   append_application_prefix(bytes, entry_kind::ranked_index_level, type);
   append_be32(bytes, ordinal);
   append_byte(bytes, level);
   return bytes;
}

[[nodiscard]] inline forge::db::core::record_key ranked_coordinator() {
   return forge::db::core::record_key{
      std::vector<std::byte>{static_cast<std::byte>(entry_kind::ranked_index_coordinator)}};
}

} // namespace forge::db::object::detail::record_key

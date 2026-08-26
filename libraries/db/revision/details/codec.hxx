#pragma once

namespace forge::db::revision::detail {

template <typename T>
[[nodiscard]] std::vector<std::byte> encode(const T& value) {
   const auto packed = forge::raw::pack(value);
   auto result = std::vector<std::byte>{};
   result.reserve(packed.size());
   for (const auto byte : packed) {
      result.push_back(static_cast<std::byte>(byte));
   }
   return result;
}

template <typename T>
[[nodiscard]] T decode(const std::vector<std::byte>& value, std::string_view description) {
   auto packed = forge::raw::bytes{};
   packed.reserve(value.size());
   for (const auto byte : value) {
      packed.push_back(std::to_integer<std::uint8_t>(byte));
   }
   try {
      return forge::raw::unpack<T>(packed);
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::corrupt_state, "db revision record cannot be decoded",
                            forge::exceptions::ctx("record", description),
                            forge::exceptions::ctx("error", error.what()));
   }
}

[[nodiscard]] inline forge::db::core::record_key state_key() {
   return forge::db::object::system::access::record_key(forge::db::ids::to_object_id(state_id));
}

[[nodiscard]] inline forge::db::core::record_key entry_key(revision_id_t id) {
   return forge::db::object::system::access::record_key(forge::db::ids::to_object_id(entry::id_t{id}));
}

[[nodiscard]] inline forge::db::core::record_key delta_key(std::uint64_t id) {
   return forge::db::object::system::access::record_key(forge::db::ids::to_object_id(delta::id_t{id}));
}

} // namespace forge::db::revision::detail

module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module forge.contract.hash_id;

import forge.contract.intrinsics;

export namespace forge::contract {

struct hash_id {
   enum class raw : std::uint64_t {};

   constexpr hash_id() = default;
   constexpr explicit hash_id(raw value) : id(static_cast<std::uint64_t>(value)) {}
   constexpr explicit hash_id(std::string_view text) : id(hash(text)) {
      check(text.size() <= 64U, "hash id is longer than 64 characters");
   }

   constexpr operator raw() const noexcept {
      return raw{id};
   }

   [[nodiscard]] static constexpr std::uint64_t hash(std::string_view text) noexcept {
      auto result = std::uint64_t{5381U};
      for (const auto character : text) {
         result = ((result << 5U) + result) + static_cast<unsigned char>(character);
      }
      return result;
   }

   std::uint64_t id = 0;
};

} // namespace forge::contract

consteval forge::contract::hash_id operator""_i(const char* value, std::size_t size) {
   return forge::contract::hash_id{std::string_view{value, size}};
}

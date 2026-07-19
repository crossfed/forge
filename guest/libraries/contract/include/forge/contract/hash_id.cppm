module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

export module forge.contract.hash_id;

import forge.contract.intrinsics;

export namespace forge::contract {

struct hash_id {
   static constexpr std::uint32_t max_length = 128U;

   enum class raw : std::uint64_t {};

   constexpr hash_id() = default;
   constexpr explicit hash_id(raw value) : id(static_cast<std::uint64_t>(value)) {}
   constexpr explicit hash_id(std::string_view text) : id(hash(text)) {
      validate(text);
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

   constexpr void validate(std::string_view text) const {
      if (text.empty()) {
         check(false, "string cannot be empty to be an hash_id");
      }
      if (text.size() > max_length) {
         check(false, "string is too long be a valid hash_id. must be less than or equal to 128");
      }

      const auto valid_first = [](char value) constexpr {
         return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
      };
      const auto valid_rest = [valid_first](char value) constexpr {
         return valid_first(value) || (value >= '0' && value <= '9');
      };

      if (!valid_first(text.front())) {
         check(false, "string must start with a letter or _ to be a valid hash_id.");
      }
      for (const auto value : text.substr(1U)) {
         if (!valid_rest(value)) {
            check(false, std::string{"string contains a character "} + value + " that is not a letter, number, or _");
         }
      }
   }

   std::uint64_t id = 0;
};

} // namespace forge::contract

export consteval forge::contract::hash_id operator""_i(const char* value, std::size_t size) {
   return forge::contract::hash_id{std::string_view{value, size}};
}

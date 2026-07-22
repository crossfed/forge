module;

#include <forge/exceptions/policy.hpp>

#include <cstddef>
#include <cstdint>
#if !defined(FORGE_CONTRACT_GUEST)
#include <stdexcept>
#endif
#include <string>
#include <string_view>

export module forge.chain.protocol.hash_id;

export namespace forge::chain::protocol {

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
         FORGE_POLICY_THROW_STANDARD(std::invalid_argument, "string cannot be empty to be an hash_id");
      }
      if (text.size() > max_length) {
         FORGE_POLICY_THROW_STANDARD(std::invalid_argument,
                                     "string is too long be a valid hash_id. must be less than or equal to 128");
      }

      const auto valid_first = [](char value) constexpr {
         return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
      };
      const auto valid_rest = [valid_first](char value) constexpr {
         return valid_first(value) || (value >= '0' && value <= '9');
      };

      if (!valid_first(text.front())) {
         FORGE_POLICY_THROW_STANDARD(std::invalid_argument,
                                     "string must start with a letter or _ to be a valid hash_id.");
      }
      for (const auto value : text.substr(1U)) {
         if (!valid_rest(value)) {
            FORGE_POLICY_THROW_STANDARD(std::invalid_argument, std::string{"string contains a character "} + value +
                                                                   " that is not a letter, number, or _");
         }
      }
   }

   std::uint64_t id = 0;
};

} // namespace forge::chain::protocol

module;

#if defined(FORGE_CONTRACT_GUEST)
#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <tuple>
#include <variant>
#include <vector>
#endif

export module forge.crypto.asymmetric:value;

#if defined(FORGE_CONTRACT_GUEST)
import forge.raw.codec;

export namespace forge::crypto::asymmetric {

using ecc_public_key = std::array<char, 33>;
using ecc_signature = std::array<char, 65>;

struct webauthn_public_key {
   enum class user_presence_t : std::uint8_t {
      USER_PRESENCE_NONE,
      USER_PRESENCE_PRESENT,
      USER_PRESENCE_VERIFIED,
   };

   ecc_public_key key{};
   user_presence_t user_presence = user_presence_t::USER_PRESENCE_NONE;
   std::string rpid;

   bool operator==(const webauthn_public_key&) const = default;
   auto operator<=>(const webauthn_public_key&) const = default;
};

struct webauthn_signature {
   ecc_signature compact_signature{};
   std::vector<std::uint8_t> auth_data;
   std::string client_json;

   bool operator==(const webauthn_signature&) const = default;
   auto operator<=>(const webauthn_signature&) const = default;
};

using public_key = std::variant<ecc_public_key, ecc_public_key, webauthn_public_key>;
using signature = std::variant<ecc_signature, ecc_signature, webauthn_signature>;

template <typename Stream> void raw_pack(Stream& stream, const webauthn_public_key& value) {
   forge::raw::pack(stream, value.key);
   forge::raw::pack(stream, value.user_presence);
   forge::raw::pack(stream, value.rpid);
}

template <typename Stream> void raw_unpack(Stream& stream, webauthn_public_key& value) {
   forge::raw::unpack(stream, value.key);
   forge::raw::unpack(stream, value.user_presence);
   forge::raw::unpack(stream, value.rpid);
}

template <typename Stream> void raw_pack(Stream& stream, const webauthn_signature& value) {
   forge::raw::pack(stream, value.compact_signature);
   forge::raw::pack(stream, value.auth_data);
   forge::raw::pack(stream, value.client_json);
}

template <typename Stream> void raw_unpack(Stream& stream, webauthn_signature& value) {
   forge::raw::unpack(stream, value.compact_signature);
   forge::raw::unpack(stream, value.auth_data);
   forge::raw::unpack(stream, value.client_json);
}

} // namespace forge::crypto::asymmetric
#endif

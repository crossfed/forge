#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

import forge.codec.base64;

namespace eosio {

inline std::string base64_encode(std::string_view value) {
   return forge::codec::base64::encode(value);
}

inline std::string base64_decode(std::string_view value) {
   const auto decoded = forge::codec::base64::decode(value, {.ignore_ascii_whitespace = true});
   return {decoded.begin(), decoded.end()};
}

inline std::string base64url_encode(std::string_view value) {
   return forge::codec::base64::encode(
       value, {.characters = forge::codec::base64::alphabet::url, .pad = forge::codec::base64::padding::omit});
}

inline std::string base64url_decode(std::string_view value) {
   const auto decoded = forge::codec::base64::decode(value, {.characters = forge::codec::base64::alphabet::url,
                                                             .pad = forge::codec::base64::padding_policy::allow,
                                                             .ignore_ascii_whitespace = true});
   return {decoded.begin(), decoded.end()};
}

} // namespace eosio

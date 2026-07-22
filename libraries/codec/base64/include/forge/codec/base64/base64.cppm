module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.codec.base64;

export import forge.codec.base64.exceptions;

export namespace forge::codec::base64 {

enum class alphabet {
   standard,
   url,
};

enum class padding {
   include,
   omit,
};

enum class padding_policy {
   require,
   allow,
   forbid,
};

struct encode_options {
   alphabet characters = alphabet::standard;
   padding pad = padding::include;
   std::size_t line_width = 0;
};

struct decode_options {
   alphabet characters = alphabet::standard;
   padding_policy pad = padding_policy::allow;
   bool ignore_ascii_whitespace = false;
};

[[nodiscard]] std::string encode(std::span<const std::uint8_t> input, encode_options options = {});
[[nodiscard]] std::string encode(std::string_view input, encode_options options = {});
[[nodiscard]] std::vector<std::uint8_t> decode(std::string_view input, decode_options options = {});

} // namespace forge::codec::base64

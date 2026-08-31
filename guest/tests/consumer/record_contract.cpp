#include <cstdint>
#include <string>
#include <vector>

import forge.contract;
import forge.raw.codec;

namespace contract_types {

struct record_value {
   std::string user;
   std::vector<std::uint32_t> values;
};

struct stream_bool_value {
   bool value = false;
   std::uint8_t marker = 0;

   bool operator==(const stream_bool_value&) const = default;

   template <typename Stream> friend Stream& operator<<(Stream& stream, const stream_bool_value& item) {
      return stream << item.value << item.marker;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, stream_bool_value& item) {
      return stream >> item.value >> item.marker;
   }
};

} // namespace contract_types

class [[forge::contract("recordtest")]] record_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] contract_types::record_value echo(contract_types::record_value value) {
      const auto packed = forge::raw::pack(value);
      return forge::raw::unpack_exact<contract_types::record_value>(packed);
   }

   [[forge::action]] std::vector<std::uint8_t> boolwire(bool value) {
      const auto expected = contract_types::stream_bool_value{.value = value, .marker = 0xa5U};
      auto packed = forge::raw::pack(expected);
      forge::contract::check(forge::raw::unpack_exact<contract_types::stream_bool_value>(packed) == expected,
                             "stream bool round-trip failed");
      return packed;
   }
};

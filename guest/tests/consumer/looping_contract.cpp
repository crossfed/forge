#include <forge/contract/serialize.hpp>

#include <cstdint>

import forge.chain.protocol.values;
import forge.contract;
import forge.contract.call;
import forge.contract.multi_index;

namespace looping {

using forge::chain::protocol::literals::operator""_n;

constexpr auto callee = forge::chain::protocol::make_name("loopcallee");

struct [[forge::table("looprows")]] loop_record {
   std::uint64_t id = 0;
   std::uint64_t marker = 0;

   [[nodiscard]] std::uint64_t primary_key() const {
      return id;
   }

   FORGE_SERIALIZE(loop_record, &loop_record::id, &loop_record::marker)
};

using loop_rows = forge::contract::multi_index<"looprows"_n, loop_record>;

} // namespace looping

class [[forge::contract("looping")]] looping_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void warmup() {
      auto rows = looping::loop_rows{get_self(), get_self().value};
      rows.emplace(get_self(), [](auto& value) { value.id = 1U; });
   }

   [[forge::action]] void direct() {
      write_marker();
      spin();
   }

   [[forge::action]] void nested() {
      write_marker();
      const auto invoke = forge::contract::call_wrapper<"spin"_i, &looping_contract::spin>{looping::callee};
      invoke();
   }

   [[forge::call]] void spin() const {
      while (true) {
      }
   }

 private:
   void write_marker() {
      auto rows = looping::loop_rows{get_self(), get_self().value};
      rows.modify(rows.require_find(1U), forge::contract::same_payer, [](auto& value) { value.marker = 1U; });
   }
};

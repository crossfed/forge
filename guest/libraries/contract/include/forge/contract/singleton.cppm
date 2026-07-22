module;

#include <cstdint>

export module forge.contract.singleton;

import forge.chain.protocol.values;
import forge.contract.intrinsics;
import forge.contract.multi_index;
import forge.raw.codec;

namespace forge::contract::detail {

template <std::uint64_t Primary, class T> struct singleton_row {
   T value;

   [[nodiscard]] constexpr std::uint64_t primary_key() const noexcept {
      return Primary;
   }
};

} // namespace forge::contract::detail

namespace forge::raw {

template <std::uint64_t Primary, class T> struct codec_traits<forge::contract::detail::singleton_row<Primary, T>> {
   template <class Stream>
   static void pack(Stream& stream, const forge::contract::detail::singleton_row<Primary, T>& row) {
      forge::raw::pack(stream, row.value);
   }

   template <class Stream> static void unpack(Stream& stream, forge::contract::detail::singleton_row<Primary, T>& row) {
      forge::raw::unpack(stream, row.value);
   }
};

} // namespace forge::raw

export namespace forge::contract {

template <chain::protocol::name::raw SingletonName, class T> class singleton {
   static constexpr auto primary = static_cast<std::uint64_t>(SingletonName);
   using row = detail::singleton_row<primary, T>;
   using table = multi_index<SingletonName, row>;

 public:
   singleton(chain::protocol::name code, std::uint64_t scope) : table_{code, scope} {}

   [[nodiscard]] bool exists() const {
      return table_.find(primary) != table_.end();
   }

   [[nodiscard]] T get() const {
      const auto iterator = table_.find(primary);
      check(iterator != table_.end(), "singleton does not exist");
      return iterator->value;
   }

   [[nodiscard]] T get_or_default(const T& default_value = T{}) const {
      const auto iterator = table_.find(primary);
      return iterator == table_.end() ? default_value : iterator->value;
   }

   [[nodiscard]] T get_or_create(chain::protocol::name payer, const T& default_value = T{}) {
      const auto iterator = table_.find(primary);
      if (iterator != table_.end()) {
         return iterator->value;
      }
      return table_.emplace(payer, [&](row& value) { value.value = default_value; })->value;
   }

   void set(const T& value, chain::protocol::name payer) {
      const auto iterator = table_.find(primary);
      if (iterator == table_.end()) {
         table_.emplace(payer, [&](row& stored) { stored.value = value; });
      } else {
         table_.modify(iterator, payer, [&](row& stored) { stored.value = value; });
      }
   }

   void remove() {
      const auto iterator = table_.find(primary);
      if (iterator != table_.end()) {
         table_.erase(iterator);
      }
   }

 private:
   table table_;
};

} // namespace forge::contract

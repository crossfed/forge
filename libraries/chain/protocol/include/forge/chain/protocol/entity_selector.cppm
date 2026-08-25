module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <optional>

export module forge.chain.protocol.entity_selector;

import forge.chain.protocol.native_ids;
import forge.chain.protocol.types;
import forge.raw.codec;

#if !defined(FORGE_CONTRACT_GUEST)
export import forge.db.ids.object_id;
#endif

export namespace forge::chain::protocol {

template <typename Id, typename Key> struct entity_selector {
   std::optional<Id> id;
   std::optional<Key> key;

   bool operator==(const entity_selector&) const = default;
};

template <typename Stream, typename Id, typename Key>
void raw_pack(Stream& stream, const entity_selector<Id, Key>& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.key);
}

template <typename Stream, typename Id, typename Key> void raw_unpack(Stream& stream, entity_selector<Id, Key>& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.key);
}

template <typename Id, typename Key>
[[nodiscard]] constexpr bool selects_exactly_one(const entity_selector<Id, Key>& value) noexcept {
   return value.id.has_value() != value.key.has_value();
}

using account_selector = entity_selector<account_id, account_name>;

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_selector, (), (id, key))
} // namespace forge::chain::protocol
#endif

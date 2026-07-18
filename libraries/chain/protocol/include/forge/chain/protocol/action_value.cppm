module;

#include <vector>

export module forge.chain.protocol.action:value;

export import forge.chain.protocol.types;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct action_base {
   account_name account;
   action_name name;
   std::vector<permission_level> authorization;
};

struct action : action_base {
   bytes data;
};

template <typename Stream> void raw_pack(Stream& stream, const action_base& value) {
   forge::raw::pack(stream, value.account);
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.authorization);
}

template <typename Stream> void raw_unpack(Stream& stream, action_base& value) {
   forge::raw::unpack(stream, value.account);
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.authorization);
}

template <typename Stream> void raw_pack(Stream& stream, const action& value) {
   raw_pack(stream, static_cast<const action_base&>(value));
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, action& value) {
   raw_unpack(stream, static_cast<action_base&>(value));
   forge::raw::unpack(stream, value.data);
}

} // namespace forge::chain::protocol

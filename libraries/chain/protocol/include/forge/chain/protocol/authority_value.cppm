module;

#include <cstdint>
#include <vector>

export module forge.chain.protocol.authority:value;

export import forge.chain.protocol.types;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct permission_level_weight {
   permission_level permission;
   weight weight = 0;

   bool operator==(const permission_level_weight&) const = default;
};

struct key_weight {
   public_key key;
   weight weight = 0;

   bool operator==(const key_weight&) const = default;
};

struct wait_weight {
   std::uint32_t wait_sec = 0;
   weight weight = 0;

   bool operator==(const wait_weight&) const = default;
};

struct authority {
   std::uint32_t threshold = 0;
   std::vector<key_weight> keys;
   std::vector<permission_level_weight> accounts;
   std::vector<wait_weight> waits;

   bool operator==(const authority&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const permission_level_weight& value) {
   forge::raw::pack(stream, value.permission);
   forge::raw::pack(stream, value.weight);
}

template <typename Stream> void raw_unpack(Stream& stream, permission_level_weight& value) {
   forge::raw::unpack(stream, value.permission);
   forge::raw::unpack(stream, value.weight);
}

template <typename Stream> void raw_pack(Stream& stream, const key_weight& value) {
   forge::raw::pack(stream, value.key);
   forge::raw::pack(stream, value.weight);
}

template <typename Stream> void raw_unpack(Stream& stream, key_weight& value) {
   forge::raw::unpack(stream, value.key);
   forge::raw::unpack(stream, value.weight);
}

template <typename Stream> void raw_pack(Stream& stream, const wait_weight& value) {
   forge::raw::pack(stream, value.wait_sec);
   forge::raw::pack(stream, value.weight);
}

template <typename Stream> void raw_unpack(Stream& stream, wait_weight& value) {
   forge::raw::unpack(stream, value.wait_sec);
   forge::raw::unpack(stream, value.weight);
}

template <typename Stream> void raw_pack(Stream& stream, const authority& value) {
   forge::raw::pack(stream, value.threshold);
   forge::raw::pack(stream, value.keys);
   forge::raw::pack(stream, value.accounts);
   forge::raw::pack(stream, value.waits);
}

template <typename Stream> void raw_unpack(Stream& stream, authority& value) {
   forge::raw::unpack(stream, value.threshold);
   forge::raw::unpack(stream, value.keys);
   forge::raw::unpack(stream, value.accounts);
   forge::raw::unpack(stream, value.waits);
}

} // namespace forge::chain::protocol

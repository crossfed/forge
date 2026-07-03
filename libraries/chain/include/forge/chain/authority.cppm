module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <new>
#include <vector>

export module forge.chain.authority;

export import forge.chain.types;
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain {


struct permission_level_weight {
   permission_level permission;
   weight weight = 0;
};

struct key_weight {
   public_key key;
   weight weight = 0;
};

struct wait_weight {
   std::uint32_t wait_sec = 0;
   weight weight = 0;
};

struct authority {
   std::uint32_t threshold = 0;
   std::vector<key_weight> keys;
   std::vector<permission_level_weight> accounts;
   std::vector<wait_weight> waits;
};

} // namespace forge::chain

export namespace forge::chain {
BOOST_DESCRIBE_STRUCT(permission_level_weight, (), (permission, weight))
BOOST_DESCRIBE_STRUCT(key_weight, (), (key, weight))
BOOST_DESCRIBE_STRUCT(wait_weight, (), (wait_sec, weight))
BOOST_DESCRIBE_STRUCT(authority, (), (threshold, keys, accounts, waits))
}

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::permission_level_weight)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::key_weight)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::wait_weight)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::authority)

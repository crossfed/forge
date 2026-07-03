module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <new>
#include <vector>

export module forge.chain.system;

export import forge.chain.authority;
export import forge.chain.types;
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain {


struct newaccount {
   account_name creator;
   account_name name;
   forge::chain::authority owner;
   forge::chain::authority active;

   static action_name get_name();
};

struct setcode {
   account_name account;
   std::uint8_t vmtype = 0;
   std::uint8_t vmversion = 0;
   bytes code;

   static action_name get_name();
};

struct setabi {
   account_name account;
   bytes abi;

   static action_name get_name();
};

struct updateauth {
   account_name account;
   permission_name permission;
   permission_name parent;
   forge::chain::authority auth;

   static action_name get_name();
};

struct deleteauth {
   account_name account;
   permission_name permission;

   static action_name get_name();
};

struct linkauth {
   account_name account;
   account_name code;
   action_name type;
   permission_name requirement;

   static action_name get_name();
};

struct unlinkauth {
   account_name account;
   account_name code;
   action_name type;

   static action_name get_name();
};

struct canceldelay {
   permission_level canceling_auth;
   transaction_id trx_id;

   static action_name get_name();
};

struct onerror {
   uint128_t sender_id = 0;
   bytes sent_trx;

   static action_name get_name();
};

} // namespace forge::chain

export namespace forge::chain {
BOOST_DESCRIBE_STRUCT(newaccount, (), (creator, name, owner, active))
BOOST_DESCRIBE_STRUCT(setcode, (), (account, vmtype, vmversion, code))
BOOST_DESCRIBE_STRUCT(setabi, (), (account, abi))
BOOST_DESCRIBE_STRUCT(updateauth, (), (account, permission, parent, auth))
BOOST_DESCRIBE_STRUCT(deleteauth, (), (account, permission))
BOOST_DESCRIBE_STRUCT(linkauth, (), (account, code, type, requirement))
BOOST_DESCRIBE_STRUCT(unlinkauth, (), (account, code, type))
BOOST_DESCRIBE_STRUCT(canceldelay, (), (canceling_auth, trx_id))
BOOST_DESCRIBE_STRUCT(onerror, (), (sender_id, sent_trx))
}

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::newaccount)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::setcode)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::setabi)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::updateauth)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::deleteauth)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::linkauth)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::unlinkauth)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::canceldelay)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::onerror)

module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.account_ram_correction;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct account_ram_correction {
   account_ram_correction_id id;
   account_name name;
   std::uint64_t ram_correction = 0;

   bool operator==(const account_ram_correction&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_ram_correction, (), (id, name, ram_correction))
} // namespace forge::chain::protocol
#endif

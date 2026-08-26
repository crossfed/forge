module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.code;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct code {
   code_id id;
   digest code_hash;
   std::uint64_t code_size = 0;
   std::uint64_t code_ref_count = 0;
   std::uint32_t first_block_used = 0;
   std::uint8_t vm_type = 0;
   std::uint8_t vm_version = 0;

   bool operator==(const code&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(code, (), (id, code_hash, code_size, code_ref_count, first_block_used, vm_type, vm_version))
} // namespace forge::chain::protocol
#endif

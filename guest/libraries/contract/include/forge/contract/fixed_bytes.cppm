module;

#include <cstddef>

export module forge.contract.fixed_bytes;

export import forge.chain.protocol.fixed_key;
export import forge.chain.protocol.types;

export namespace forge::contract {

template <std::size_t Size> using fixed_bytes = chain::protocol::fixed_key<Size>;
using checksum160 = chain::protocol::checksum160;
using checksum256 = chain::protocol::checksum256;
using checksum512 = chain::protocol::checksum512;
using key256 = chain::protocol::key256;

} // namespace forge::contract

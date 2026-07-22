module;

#include <cstdint>
#include <utility>
#include <vector>

export module forge.chain.protocol.types:value;

export import forge.chain.protocol.time;
export import forge.chain.protocol.values;

import forge.crypto.ripemd160;
import forge.crypto.sha256;
import forge.crypto.sha512;
import forge.crypto.asymmetric.value;

export namespace forge::chain::protocol {

using bytes = std::vector<std::uint8_t>;
using digest = forge::crypto::sha256;
using chain_id = digest;
using block_id = digest;
using checksum = digest;
using checksum256 = digest;
using checksum512 = forge::crypto::sha512;
using checksum160 = forge::crypto::ripemd160;
using transaction_id = checksum;
using public_key = forge::crypto::asymmetric::public_key;
using signature = forge::crypto::asymmetric::signature;
using weight = std::uint16_t;
using block_num = std::uint32_t;
using share = std::int64_t;
using extensions = std::vector<std::pair<std::uint16_t, bytes>>;

} // namespace forge::chain::protocol

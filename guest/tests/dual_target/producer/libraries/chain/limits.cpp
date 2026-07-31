module;

#include <cstdint>

module product.chain.limits;

import forge.crypto.digest.sha256;

namespace product::chain {

bool is_supported_size(std::uint64_t value) {
   return sizeof(forge::crypto::digest::sha256) == 32 && value != 0;
}

} // namespace product::chain

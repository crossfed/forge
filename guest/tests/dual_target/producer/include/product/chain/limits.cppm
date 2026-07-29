module;

#include <cstdint>

export module product.chain.limits;

import forge.crypto.digest.sha256;

export namespace product::chain {

[[nodiscard]] bool is_supported_size(std::uint64_t value);

} // namespace product::chain

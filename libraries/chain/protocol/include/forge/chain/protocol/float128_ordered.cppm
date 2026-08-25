module;

export module forge.chain.protocol.float128:ordered;

import :value;
export import forge.chain.protocol.exceptions;
import forge.chain.protocol.fixed_key;

export namespace forge::chain::protocol {

[[nodiscard]] fixed_key<16> ordered_key(float128 value);

} // namespace forge::chain::protocol

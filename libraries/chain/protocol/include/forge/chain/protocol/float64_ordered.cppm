module;

export module forge.chain.protocol.float64:ordered;

import :value;
export import forge.chain.protocol.exceptions;
import forge.chain.protocol.fixed_key;

export namespace forge::chain::protocol {

[[nodiscard]] fixed_key<8> ordered_key(float64 value);

} // namespace forge::chain::protocol

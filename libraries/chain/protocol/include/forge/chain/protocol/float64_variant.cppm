module;

export module forge.chain.protocol.float64:variant;

import :value;
import forge.variant.value;

export namespace forge {

void to_variant(const forge::chain::protocol::float64& value, forge::variant& output);
void from_variant(const forge::variant& input, forge::chain::protocol::float64& output);

} // namespace forge

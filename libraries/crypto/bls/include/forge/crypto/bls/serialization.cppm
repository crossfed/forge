module;

#include <string>
#include <string_view>

export module forge.crypto.bls.serialization;

export import forge.crypto.bls.exceptions;
export import forge.crypto.bls.values;

import forge.variant.value;

export namespace forge::crypto::bls {

namespace encoding {

[[nodiscard]] public_key parse_public_key(std::string_view text);
[[nodiscard]] signature parse_signature(std::string_view text);
[[nodiscard]] aggregate_signature parse_aggregate_signature(std::string_view text);

[[nodiscard]] std::string format(const public_key& value);
[[nodiscard]] std::string format(const signature& value);
[[nodiscard]] std::string format(const aggregate_signature& value);

} // namespace encoding

void to_variant(const public_key& value, variant& output);
void from_variant(const variant& value, public_key& output);
void to_variant(const signature& value, variant& output);
void from_variant(const variant& value, signature& output);
void to_variant(const aggregate_signature& value, variant& output);
void from_variant(const variant& value, aggregate_signature& output);

} // namespace forge::crypto::bls

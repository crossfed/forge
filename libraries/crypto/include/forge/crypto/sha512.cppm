module;

export module forge.crypto.sha512;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
export import forge.crypto.digest;
import forge.core.string;
import forge.variant.chrono;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.exceptions;
import forge.variant.format;
import forge.variant.multiprecision;
import forge.variant.value;

export namespace forge::crypto {

void to_variant(const sha512& value, variant& result);
void from_variant(const variant& value, sha512& result);

} // namespace forge::crypto
#endif

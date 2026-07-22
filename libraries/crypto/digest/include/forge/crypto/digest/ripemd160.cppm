module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <functional>
#endif

export module forge.crypto.digest.ripemd160;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
export import forge.crypto.digest;
import forge.core.type_name;
import forge.variant.chrono;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.exceptions;
import forge.variant.format;
import forge.variant.multiprecision;
import forge.variant.value;

export namespace forge::crypto::digest {

void to_variant(const ripemd160& value, variant& result);
void from_variant(const variant& value, ripemd160& result);

} // namespace forge::crypto

export template <> struct forge::get_typename<forge::crypto::digest::uint160_t> {
   static const char* name() {
      return "uint160_t";
   }
};

export namespace std {

template <> struct hash<forge::crypto::digest::ripemd160> {
   std::size_t operator()(const forge::crypto::digest::ripemd160& value) const noexcept {
      return static_cast<std::size_t>(value._hash[0]);
   }
};

} // namespace std
#endif

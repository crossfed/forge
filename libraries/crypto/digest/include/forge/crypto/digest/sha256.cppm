module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/functional/hash.hpp>
#include <cstddef>
#include <functional>
#include <ostream>
#endif

export module forge.crypto.digest.sha256;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
export import forge.crypto.digest;
import forge.core.string;
import forge.raw.raw;
import forge.variant.chrono;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.exceptions;
import forge.variant.format;
import forge.variant.multiprecision;
import forge.variant.value;

export namespace forge::crypto::digest {

void to_variant(const sha256& value, variant& result);
void from_variant(const variant& value, sha256& result);

std::uint64_t hash64(const char* data, std::size_t size);

} // namespace forge::crypto

export namespace std {

template <> struct hash<forge::crypto::digest::sha256> {
   std::size_t operator()(const forge::crypto::digest::sha256& value) const noexcept {
      return static_cast<std::size_t>(value._hash[0]);
   }
};

inline std::ostream& operator<<(std::ostream& stream, const forge::crypto::digest::sha256& value) {
   return stream << "sha256(" << value.str() << ')';
}

} // namespace std

export namespace boost {

template <> struct hash<forge::crypto::digest::sha256> {
   std::size_t operator()(const forge::crypto::digest::sha256& value) const noexcept {
      return forge::crypto::digest::hash_value(value);
   }
};

} // namespace boost
#endif

module;
#include <forge/exceptions/macros.hpp>
#include <cstring>
#include <exception>
#include <memory>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string>
#include <vector>

module forge.crypto.digest.ripemd160;

import forge.core.utility;
import forge.codec.hex;
import forge.crypto.digest.sha256;
import forge.crypto.digest.sha512;
import forge.exceptions;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

#include "details/digest_common.hxx"
#include "details/evp_digest_context.hxx"

namespace forge::crypto::digest {

ripemd160::ripemd160(const std::string& hex_str) {
   auto bytes_written = forge::codec::hex::decode(
       hex_str, std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(_hash), sizeof(_hash)});
   if (bytes_written < sizeof(_hash))
      memset((char*)_hash + bytes_written, 0, (sizeof(_hash) - bytes_written));
}

std::string ripemd160::str() const {
   return forge::codec::hex::encode(
       std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(_hash), sizeof(_hash)});
}
ripemd160::operator std::string() const {
   return str();
}

struct ripemd160::encoder::impl {
   forge::detail::evp_digest_context ctx;
};

ripemd160::encoder::~encoder() {}
ripemd160::encoder::encoder() : my(std::make_unique<impl>()) {
   reset();
}

ripemd160 ripemd160::hash(const forge::crypto::digest::sha512& h) {
   return hash((const char*)&h, sizeof(h));
}
ripemd160 ripemd160::hash(const forge::crypto::digest::sha256& h) {
   return hash((const char*)&h, sizeof(h));
}
ripemd160 ripemd160::hash(const char* d, uint32_t dlen) {
   encoder e;
   e.write(d, dlen);
   return e.result();
}
ripemd160 ripemd160::hash(const std::string& s) {
   return hash(s.c_str(), s.size());
}

void ripemd160::encoder::write(const char* d, uint32_t dlen) {
   forge::detail::evp_digest_update(my->ctx.get(), d, dlen);
}
ripemd160 ripemd160::encoder::result() {
   ripemd160 h;
   forge::detail::evp_digest_final(my->ctx.get(), h.data(), h.data_size());
   return h;
}
void ripemd160::encoder::reset() {
   forge::detail::evp_digest_init(my->ctx.get(), EVP_ripemd160());
}

ripemd160 operator<<(const ripemd160& h1, uint32_t i) {
   ripemd160 result;
   forge::detail::shift_l(h1.data(), result.data(), result.data_size(), i);
   return result;
}
ripemd160 operator^(const ripemd160& h1, const ripemd160& h2) {
   ripemd160 result;
   result._hash[0] = h1._hash[0] ^ h2._hash[0];
   result._hash[1] = h1._hash[1] ^ h2._hash[1];
   result._hash[2] = h1._hash[2] ^ h2._hash[2];
   result._hash[3] = h1._hash[3] ^ h2._hash[3];
   result._hash[4] = h1._hash[4] ^ h2._hash[4];
   return result;
}
void to_variant(const ripemd160& bi, variant& v) {
   v = std::vector<char>((const char*)&bi, ((const char*)&bi) + sizeof(bi));
}
void from_variant(const variant& v, ripemd160& bi) {
   std::vector<char> ve = v.as<std::vector<char>>();
   if (ve.size()) {
      memcpy(bi.data(), ve.data(), forge::min<size_t>(ve.size(), sizeof(bi)));
   } else
      memset(bi.data(), char(0), sizeof(bi));
}

} // namespace forge::crypto

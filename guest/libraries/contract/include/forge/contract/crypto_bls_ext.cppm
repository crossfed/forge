module;

#include <cstdint>
#include <string>
#include <string_view>

export module forge.contract.crypto_bls_ext;

export import forge.contract.crypto;
export import forge.crypto.bls.values;

export namespace forge::contract {

using bls_scalar = forge::crypto::bls::scalar;
using bls_fp = forge::crypto::bls::field_element;
using bls_s = forge::crypto::bls::wide_scalar;
using bls_fp2 = forge::crypto::bls::field_element2;
using bls_g1 = forge::crypto::bls::g1;
using bls_g2 = forge::crypto::bls::g2;
using bls_gt = forge::crypto::bls::gt;

[[nodiscard]] std::int32_t bls_g1_add(const bls_g1& first, const bls_g1& second, bls_g1& result);
[[nodiscard]] std::int32_t bls_g2_add(const bls_g2& first, const bls_g2& second, bls_g2& result);
[[nodiscard]] std::int32_t bls_g1_weighted_sum(const bls_g1 points[], const bls_scalar scalars[], std::uint32_t count,
                                               bls_g1& result);
[[nodiscard]] std::int32_t bls_g2_weighted_sum(const bls_g2 points[], const bls_scalar scalars[], std::uint32_t count,
                                               bls_g2& result);
[[nodiscard]] std::int32_t bls_pairing(const bls_g1 first[], const bls_g2 second[], std::uint32_t count,
                                       bls_gt& result);
[[nodiscard]] std::int32_t bls_g1_map(const bls_fp& value, bls_g1& result);
[[nodiscard]] std::int32_t bls_g2_map(const bls_fp2& value, bls_g2& result);
[[nodiscard]] std::int32_t bls_fp_mod(const bls_s& value, bls_fp& result);
[[nodiscard]] std::int32_t bls_fp_mul(const bls_fp& first, const bls_fp& second, bls_fp& result);
[[nodiscard]] std::int32_t bls_fp_exp(const bls_fp& base, const bls_s& exponent, bls_fp& result);

[[nodiscard]] std::string encode_g1_to_bls_public_key(const bls_g1& value);
[[nodiscard]] bls_g1 decode_bls_public_key_to_g1(std::string_view value);
[[nodiscard]] std::string encode_g2_to_bls_signature(const bls_g2& value);
[[nodiscard]] bls_g2 decode_bls_signature_to_g2(std::string_view value);
[[nodiscard]] bool bls_pop_verify(const bls_g1& public_key, const bls_g2& proof);
[[nodiscard]] bool bls_signature_verify(const bls_g1& public_key, const bls_g2& signature, std::string_view message);

} // namespace forge::contract

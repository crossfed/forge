module;

#include <array>

export module forge.crypto.bls.values;

export namespace forge::crypto::bls {

using scalar = std::array<char, 32>;
using field_element = std::array<char, 48>;
using wide_scalar = std::array<char, 64>;
using field_element2 = std::array<field_element, 2>;
using g1 = std::array<char, 96>;
using g2 = std::array<char, 192>;
using gt = std::array<char, 576>;

using public_key_value = g1;
using signature_value = g2;

} // namespace forge::crypto::bls

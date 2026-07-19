#pragma once

import forge.contract.crypto_bls_ext;

namespace eosio {

using forge::contract::bls_fp;
using forge::contract::bls_fp2;
using forge::contract::bls_fp_exp;
using forge::contract::bls_fp_mod;
using forge::contract::bls_fp_mul;
using forge::contract::bls_pop_verify;
using forge::contract::bls_g1;
using forge::contract::bls_g1_add;
using forge::contract::bls_g1_map;
using forge::contract::bls_g1_weighted_sum;
using forge::contract::bls_g2;
using forge::contract::bls_g2_add;
using forge::contract::bls_g2_map;
using forge::contract::bls_g2_weighted_sum;
using forge::contract::bls_gt;
using forge::contract::bls_pairing;
using forge::contract::bls_signature_verify;
using forge::contract::bls_s;
using forge::contract::bls_scalar;
using forge::contract::decode_bls_public_key_to_g1;
using forge::contract::decode_bls_signature_to_g2;
using forge::contract::encode_g1_to_bls_public_key;
using forge::contract::encode_g2_to_bls_signature;

} // namespace eosio

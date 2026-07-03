#pragma once

#include <cstdint>
#include <string_view>

namespace forge::tests::spring_fixtures {

// Spring fixtures generated from the local donor checkout:
// /Users/vladimirtarnakin/.openclaw/workspace/Projects/Storlane/spring
// Donor files: libraries/chain/transaction.cpp, block_header.cpp,
// block_state.cpp, abi_def.hpp, contract_types.hpp.
// Test key is the public default development key from Spring/Antelope tests.

inline constexpr std::string_view test_private_key =
   "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3";
inline constexpr std::string_view test_public_key =
   "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV";

inline constexpr std::string_view chain_id =
   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

inline constexpr std::uint64_t name_eosio_value = 6138663577826885632ULL;
inline constexpr std::uint64_t name_active_value = 3617214756542218240ULL;
inline constexpr std::string_view name_eosio_raw = "0000000000ea3055";
inline constexpr std::string_view asset_raw = "2a000000000000000453595300000000";

inline constexpr std::string_view action_raw =
   "0000000000ea305500000000b863b2c200020102";
inline constexpr std::string_view transaction_raw =
   "000000000100ddccbbaa00000000010000000000ea305500000000b863b2c20002010200";
inline constexpr std::string_view transaction_id =
   "38af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792";
inline constexpr std::string_view transaction_signature_preimage =
   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
   "000000000100ddccbbaa00000000010000000000ea305500000000b863b2c20002010200"
   "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";
inline constexpr std::string_view transaction_signature_digest =
   "ceb78f31325f7a3f9b1280064d5e2529aabdb3be8eb87a8c2553363de396d7d9";
inline constexpr std::string_view transaction_signature =
   "SIG_K1_JuQAtSJCwPZikqstTVpFhsuocpcusBz5MVrj2WJsBiqAsBPLWET2q3BpKNsQkSng5aDzzTTAQTW2aGL51Ym9mTQcSMuhKk";
inline constexpr std::string_view signed_transaction_raw =
   "000000000100ddccbbaa00000000010000000000ea305500000000b863b2c20002010200"
   "01001f011b3864db72d4b002d340597753013c7029a44a9beb04844b9198bd25c01bf301"
   "ef88c86fbe403f3bf9dadeb84149638aa8e7cbcaff3cc6c4a5f21b9d2e997f01020304";
inline constexpr std::string_view packed_transaction_raw =
   "01001f011b3864db72d4b002d340597753013c7029a44a9beb04844b9198bd25c01bf301"
   "ef88c86fbe403f3bf9dadeb84149638aa8e7cbcaff3cc6c4a5f21b9d2e997f000401020304"
   "24000000000100ddccbbaa00000000010000000000ea305500000000b863b2c20002010200";
inline constexpr std::string_view packed_transaction_digest =
   "1182f6b15748ddaf28b7147331d1fe7405cb91e5c9cd9000c2dd1be54e7b3457";

inline constexpr std::string_view abi_raw =
   "0e656f73696f3a3a6162692f312e32010c6163636f756e745f6e616d65046e616d6500"
   "0100000000b863b2c20673657461626900000000000000";
inline constexpr std::string_view setabi_raw = "0000000000ea3055020a0b";

inline constexpr std::string_view block_header_raw =
   "010000000000000000ea305500000000000000000000000000000000000000000000000000000000000000000000"
   "38af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792"
   "0000000000000000000000000000000000000000000000000000000000000000000000000000";
inline constexpr std::string_view block_header_signature_preimage =
   "010000000000000000ea305500000000000000000000000000000000000000000000000000000000000000000000"
   "38af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792"
   "0000000000000000000000000000000000000000000000000000000000000000000000000000";
inline constexpr std::string_view block_digest =
   "9682e49e65ef65d4244aafabf30ec509cb2f8465d90c2db2d6e23f3fccad7056";
inline constexpr std::string_view block_id =
   "0000000165ef65d4244aafabf30ec509cb2f8465d90c2db2d6e23f3fccad7056";
inline constexpr std::uint32_t block_num_from_id = 1U;
inline constexpr std::string_view block_signature =
   "SIG_K1_KVLB6NN2TWvTiQPxx9b1pWFbquoghK2vJdKYApLxHP1cz3abtBLStR1z2V4Sy9xsJo7VfZxMEq6KpUqjPeqgyzFjaqWFy1";
inline constexpr std::string_view signed_block_header_raw =
   "010000000000000000ea305500000000000000000000000000000000000000000000000000000000000000000000"
   "38af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792"
   "0000000000000000000000000000000000000000000000000000000000000000000000000000"
   "00200476406d4a4c745ae57bdaf7d06076876bcab4810cda6ccbbe0713f249f3ced17785"
   "acbe937e81c3c0ed971d2dce171b90546fd43398c058609a61a0e14bcade";
inline constexpr std::string_view transaction_receipt_raw =
   "0000000000000038af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792";
inline constexpr std::string_view transaction_receipt_digest =
   "ec64af578f7f141be25d0ff8f10bf446f5c9de3f66061d8afacc0d95ad040de4";
inline constexpr std::string_view signed_block_raw =
   "010000000000000000ea305500000000000000000000000000000000000000000000000000000000000000000000"
   "38af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792"
   "0000000000000000000000000000000000000000000000000000000000000000000000000000"
   "00200476406d4a4c745ae57bdaf7d06076876bcab4810cda6ccbbe0713f249f3ced17785"
   "acbe937e81c3c0ed971d2dce171b90546fd43398c058609a61a0e14bcade"
   "010000000000000038af6a6bf80f22283e3835f9f56d0c7845674e07a5fe9943fdd43edca7dd2792"
   "010700020a0b";

} // namespace forge::tests::spring_fixtures

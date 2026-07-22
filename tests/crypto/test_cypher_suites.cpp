#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

import forge.crypto.asymmetric;
import forge.codec.hex;
import forge.crypto.asymmetric.secp256k1;
import forge.crypto.asymmetric.p256;
import forge.crypto.asymmetric.ed25519;
import forge.crypto.asymmetric.rsa;
import forge.crypto.asymmetric.x25519;
import forge.crypto.symmetric.chacha20_poly1305;
import forge.crypto.digest.sha256;
import forge.core.utility;
import forge.exceptions;
import forge.raw.raw;

using namespace forge::crypto;
using namespace forge::crypto::asymmetric;
using namespace forge;
using forge::crypto::digest::sha256;
namespace chacha20_poly1305 = forge::crypto::symmetric::chacha20_poly1305;

BOOST_AUTO_TEST_SUITE(cypher_suites)

BOOST_AUTO_TEST_CASE(asymmetric_algorithm_preserves_raw_int32_layout) {
   static_assert(std::is_same_v<std::underlying_type_t<algorithm>, std::int32_t>);

   const auto packed = forge::raw::pack(algorithm::rsa);
   BOOST_TEST(forge::codec::hex::encode(packed) == "04000000");
   BOOST_CHECK(forge::raw::unpack<algorithm>(packed) == algorithm::rsa);
}

BOOST_AUTO_TEST_CASE(asymmetric_values_preserve_spring_wire_tags_and_bytes) {
   const auto& antelope = encoding::antelope();

   const auto k1_public = antelope.parse_public("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV");
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(k1_public)) ==
              "0002c0ded2bc1f1305fb0faac5e6c03ee3a1924234985427b6167ca569d13df435cf");

   const auto r1_public = antelope.parse_public("PUB_R1_6EPHFSKVYHBjQgxVGQPrwCxTg7BbZ69H9i4gztN9deKTEXYne4");
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(r1_public)) ==
              "0102b0deed150ac513f4e0b62d2f7669cb3b36e79e3e7f0a9e021dd013a33eee9c66");

   const auto webauthn_public =
       antelope.parse_public("PUB_WA_WdCPfafVNxVMiW5ybdNs83oWjenQXvSt1F49fg9mv7qrCiRwHj5b38U3ponCFWxQTkDsMC");
   const auto webauthn_public_raw = std::string{
       "020220b9dab512e892392a44a9f41f9433c9fbd80db864e9df5889c2407db3acbb9f010d6b656f73642e696e76616c6964"};
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(webauthn_public)) == webauthn_public_raw);
   BOOST_CHECK(forge::raw::unpack_exact<public_key>(forge::codec::hex::decode(webauthn_public_raw)) == webauthn_public);

   const auto k1_signature = antelope.parse_signature(
       "SIG_K1_K3LfbB7ZV2DNBu67iSn3yUMseTdiwoT49gAcwSZVT1QTvGXVHjkcvKqhentCW4FJngZJ1H9gBRSWgo9UPiWEXWHyKpXNCZ");
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(k1_signature)) ==
              "001f3dcb0d39f609909713176bab7fbd5a11fc4d1597c1d67595173bf824199c6da129a1bf61f232c85b93c433d63715"
              "ecfce385508ecac1dceb90e45c50969dde80");

   auto r1_bytes = ecc_signature{};
   for (auto index = std::size_t{}; index < r1_bytes.size(); ++index) {
      r1_bytes[index] = static_cast<char>(index);
   }
   const auto r1_value = signature{r1_signature{r1_bytes}};
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(r1_value)) ==
              "01000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
              "303132333435363738393a3b3c3d3e3f40");

   const auto webauthn_signature = antelope.parse_signature(
       "SIG_WA_2AAAuLJS3pLPgkQQPqLsehL6VeRBaAZS7NYM91UYRUrSAEfUvzKN7DCSwhjsDqe74cZNWKUUGAHGG8ddSA7cvUxChbfKxL"
       "SrDCpwe6MVUqz4PDdyCt5tXhEJmKekxG1o1ucY3LVj8Vi9rRbzAkKPCzWqC8cPcUtpLHNG8qUKkQrN4Xuwa9W8rsBiUKwZv1To"
       "LyVhLrJe42pvHYBXicp4E8qec5E4m6SX11KuXERFcV48Mhiie2NyaxdtNtNzQ5XZ5hjBkxRujqejpF4SNHvdAGKRBbvhkiPLA25"
       "FD3xoCbrN26z72");
   const auto webauthn_signature_raw = std::string{
       "0220d9132bbdb219e4e2d99af9c507e3597f86b615814f36672d501034861792bbcf21a46d1a2eb12bace4a29100b942f9"
       "87494f3aefc8efb2d5af4d4d8de3e0871525aa14905af60ca17a1bb80e0cf9c3b46908a0f14f72567a2f140c3a3bd2ef0"
       "74c010000006d737b226f726967696e223a2268747470733a2f2f6b656f73642e696e76616c6964222c2274797065223a22"
       "776562617574686e2e676574222c226368616c6c656e6765223a226f69567235794848304a4336453962446675347142735a"
       "6a527a70416c5131505a50436e5974766850556b3d227d"};
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(webauthn_signature)) == webauthn_signature_raw);
   BOOST_CHECK(forge::raw::unpack_exact<signature>(forge::codec::hex::decode(webauthn_signature_raw)) ==
               webauthn_signature);
}

BOOST_AUTO_TEST_CASE(forge_extension_key_tags_are_explicit) {
   const auto ed25519_public = public_key{ed25519_public_key{}};
   const auto rsa_public = public_key{rsa_public_key{{0xaaU, 0xbbU}}};
   const auto ed25519_value = signature{ed25519_signature{}};
   const auto rsa_value = signature{rsa_signature{{0xccU, 0xddU}}};

   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(ed25519_public)).starts_with("03"));
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(rsa_public)) == "0402aabb");
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(ed25519_value)).starts_with("03"));
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(rsa_value)) == "0402ccdd");
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

BOOST_AUTO_TEST_CASE(test_k1) try {
   auto private_key_string = std::string("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
   auto expected_public_key = std::string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV");
   auto test_private_key = encoding::eos().parse_private(private_key_string);
   auto test_public_key = test_private_key.get_public_key();

   BOOST_CHECK_EQUAL(private_key_string, encoding::eos().format(test_private_key));
   BOOST_CHECK_EQUAL(expected_public_key, encoding::eos().format(test_public_key));
   BOOST_CHECK(encoding::forge().format(test_public_key).starts_with("PUB_SECP256K1_"));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_p256_eos_encoding) try {
   auto private_key_string = std::string("PVT_R1_iyQmnyPEGvFd8uffnk152WC2WryBjgTrg22fXQryuGL9mU6qW");
   auto expected_public_key = std::string("PUB_R1_6EPHFSKVYHBjQgxVGQPrwCxTg7BbZ69H9i4gztN9deKTEXYne4");
   auto test_private_key = encoding::eos().parse_private(private_key_string);
   auto test_public_key = test_private_key.get_public_key();

   BOOST_CHECK_EQUAL(private_key_string, encoding::eos().format(test_private_key));
   BOOST_CHECK_EQUAL(expected_public_key, encoding::eos().format(test_public_key));
   BOOST_CHECK(encoding::forge().format(test_public_key).starts_with("PUB_P256_"));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(antelope_encoding_matches_legacy_eos_profile) try {
   const auto message = std::vector<std::uint8_t>{'a', 'n', 't', 'e', 'l', 'o', 'p', 'e'};
   const auto k1 = encoding::eos().parse_private("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
   const auto r1 = encoding::eos().parse_private("PVT_R1_iyQmnyPEGvFd8uffnk152WC2WryBjgTrg22fXQryuGL9mU6qW");

   for (const auto& key : std::vector<private_key>{k1, r1}) {
      const auto public_key = key.get_public_key();
      const auto signature = key.sign(message);

      BOOST_TEST(encoding::antelope().format(key) == encoding::eos().format(key));
      BOOST_TEST(encoding::antelope().format(public_key) == encoding::eos().format(public_key));
      BOOST_TEST(encoding::antelope().format(signature) == encoding::eos().format(signature));
      BOOST_CHECK(encoding::antelope().parse_public(encoding::eos().format(public_key)) == public_key);
      BOOST_CHECK(encoding::antelope().parse_signature(encoding::eos().format(signature)) == signature);
   }
}
FORGE_LOG_AND_RETHROW();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

BOOST_AUTO_TEST_CASE(custom_encoding_profile_controls_text_prefixes) try {
   const auto key = private_key::generate<secp256k1::private_key>();
   const auto public_key = key.get_public_key();
   const auto signature = key.sign(std::vector<std::uint8_t>{'s', 'p', 'r', 'i', 'n', 'g'});

   auto profile = text_encoding_profile{
       .id = "spring",
   };
   profile.private_keys.push_back(text_encoding_rule{
       .type = algorithm::secp256k1,
       .text_prefix = "PVT_K1_",
       .checksum = {.scheme = checksum_scheme::ripemd160_with_text_suffix, .text_suffix = "K1"},
   });
   profile.public_keys.push_back(text_encoding_rule{
       .type = algorithm::secp256k1,
       .text_prefix = "SPRING",
       .checksum = {.scheme = checksum_scheme::ripemd160},
   });
   profile.signatures.push_back(text_encoding_rule{
       .type = algorithm::secp256k1,
       .text_prefix = "SIG_K1_",
       .checksum = {.scheme = checksum_scheme::ripemd160_with_text_suffix, .text_suffix = "K1"},
   });

   const auto spring = encoding::custom(profile);
   const auto public_text = spring.format(public_key);
   const auto private_text = spring.format(key);
   const auto signature_text = spring.format(signature);

   BOOST_TEST(public_text.starts_with("SPRING"));
   BOOST_TEST(private_text.starts_with("PVT_K1_"));
   BOOST_TEST(signature_text.starts_with("SIG_K1_"));
   BOOST_CHECK(spring.parse_public(public_text) == public_key);
   BOOST_CHECK(spring.parse_private(private_text) == key);
   BOOST_CHECK(spring.parse_signature(signature_text) == signature);
   BOOST_CHECK_THROW((void)spring.parse_public(encoding::forge().format(public_key)),
                     asymmetric::exceptions::invalid_key);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(custom_encoding_rejects_unsupported_algorithm_for_profile) try {
   auto profile = text_encoding_profile{.id = "k1-only"};
   profile.public_keys.push_back(text_encoding_rule{
       .type = algorithm::secp256k1,
       .text_prefix = "PUB_K1_",
       .checksum = {.scheme = checksum_scheme::ripemd160_with_text_suffix, .text_suffix = "K1"},
   });

   const auto k1_only = encoding::custom(profile);
   const auto p256_key = private_key::generate<p256::private_key>().get_public_key();
   BOOST_CHECK_THROW((void)k1_only.format(p256_key), asymmetric::exceptions::invalid_options);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(built_in_profiles_cover_common_text_encoding_families) try {
   const auto k1 = private_key::generate<secp256k1::private_key>();
   const auto antelope_wif = std::string{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
   const auto antelope_single_sha_wif = std::string{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79xoCjBn"};
   const auto bitcoin_compressed_wif = std::string{"L4Gh6zmE7MGoBuRnbyAJajH8xGME9BdL2yAgsYrcXKnaANtNqMhs"};
   const auto bitcoin_edge_uncompressed_wif = std::string{"5HwoXVkHoRM8sL2KmNRS217n1g8mPPBomrY7yehCuXBzyAQyaGw"};
   const auto bitcoin_edge_compressed_wif = std::string{"KwntMbt59tTsj8xqpqYqRRWufyjGunvhSyeMo3NTYpFYrbWvZbdd"};
   const auto ed25519 = private_key::generate<ed25519::private_key>();
   const auto message = std::vector<std::uint8_t>{'p', 'r', 'o', 'f', 'i', 'l', 'e'};
   const auto ed25519_signature = ed25519.sign(message);

   const auto bitcoin = encoding::from_profile(profiles::bitcoin());
   const auto wif = bitcoin.format(k1);
   BOOST_TEST(!wif.empty());
   BOOST_TEST(!wif.starts_with("PVT_"));
   BOOST_CHECK(bitcoin.parse_private(wif) == k1);
   BOOST_CHECK_THROW((void)bitcoin.format(k1.get_public_key()), asymmetric::exceptions::invalid_options);

   const auto antelope = encoding::from_profile(profiles::antelope());
   const auto antelope_key = antelope.parse_private(antelope_wif);
   BOOST_CHECK(antelope.parse_private(antelope_single_sha_wif) == antelope_key);
   BOOST_CHECK_THROW((void)bitcoin.parse_private(antelope_single_sha_wif), asymmetric::exceptions::invalid_key);
   BOOST_CHECK(bitcoin.parse_private(antelope_wif) == antelope_key);
   BOOST_CHECK(bitcoin.parse_private(bitcoin_compressed_wif) == antelope_key);
   BOOST_TEST(bitcoin.format(antelope_key) == bitcoin_compressed_wif);
   BOOST_CHECK(bitcoin.parse_private(bitcoin.format(antelope_key)) == antelope_key);
   BOOST_TEST(antelope.format(antelope_key) == antelope_wif);

   const auto bitcoin_edge_key = bitcoin.parse_private(bitcoin_edge_uncompressed_wif);
   BOOST_CHECK(bitcoin_edge_key == bitcoin.parse_private(bitcoin_edge_compressed_wif));
   BOOST_TEST(bitcoin.format(bitcoin_edge_key) == bitcoin_edge_compressed_wif);

   const auto solana = encoding::from_profile(profiles::solana());
   const auto solana_public = solana.format(ed25519.get_public_key());
   const auto solana_private = solana.format(ed25519);
   const auto solana_signature = solana.format(ed25519_signature);
   BOOST_TEST(!solana_public.empty());
   BOOST_TEST(!solana_public.starts_with("PUB_"));
   BOOST_CHECK(solana.parse_public(solana_public) == ed25519.get_public_key());
   BOOST_CHECK(solana.parse_private(solana_private) == ed25519);
   BOOST_CHECK(solana.parse_signature(solana_signature) == ed25519_signature);

   const auto tezos = encoding::from_profile(profiles::tezos());
   const auto tezos_public = tezos.format(ed25519.get_public_key());
   const auto tezos_private = tezos.format(ed25519);
   const auto tezos_signature = tezos.format(ed25519_signature);
   BOOST_TEST(tezos_public.starts_with("edpk"));
   BOOST_TEST(!tezos_private.empty());
   BOOST_TEST(tezos_signature.starts_with("edsig"));
   BOOST_CHECK(tezos.parse_public(tezos_public) == ed25519.get_public_key());
   BOOST_CHECK(tezos.parse_private(tezos_private) == ed25519);
   BOOST_CHECK(tezos.parse_signature(tezos_signature) == ed25519_signature);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_secp256k1_recovery) try {
   const auto payload = std::vector<std::uint8_t>{'T', 'e', 's', 't'};
   auto digest = sha256::hash(std::span<const std::uint8_t>{payload});
   auto key = private_key::generate<secp256k1::private_key>();
   auto pub = key.get_public_key();
   auto sig = key.sign(payload);

   auto recovered_pub = recover(sig, digest);
   std::cout << encoding::forge().format(recovered_pub) << std::endl;

   BOOST_CHECK(recovered_pub == pub);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_p256_recovery) try {
   const auto payload = std::vector<std::uint8_t>{'T', 'e', 's', 't'};
   auto digest = sha256::hash(std::span<const std::uint8_t>{payload});
   auto key = private_key::generate<p256::private_key>();
   auto pub = key.get_public_key();
   auto sig = key.sign(payload);

   auto recovered_pub = recover(sig, digest);
   std::cout << encoding::forge().format(recovered_pub) << std::endl;

   BOOST_CHECK(recovered_pub == pub);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secp256k1_der_signature_matches_message_api) try {
   const auto message = std::vector<std::uint8_t>{'l', 'i', 'b', 'p', '2', 'p', '-', 'i', 'd'};
   const auto wrong_message = std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'};
   const auto key = secp256k1::private_key::generate();
   const auto signature = secp256k1::sign_der(key, message);

   BOOST_TEST(secp256k1::verify_der(key.get_public_key(), message, signature));
   BOOST_TEST(!secp256k1::verify_der(key.get_public_key(), wrong_message, signature));

   auto malformed = signature;
   malformed.front() ^= 0xffU;
   BOOST_CHECK_THROW((void)secp256k1::verify_der(key.get_public_key(), message, malformed),
                     secp256k1::exceptions::invalid_signature);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(p256_der_signature_matches_message_api) try {
   const auto message = std::vector<std::uint8_t>{'l', 'i', 'b', 'p', '2', 'p', '-', 'e', 'c', 'd', 's', 'a'};
   const auto wrong_message = std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'};
   const auto key = p256::private_key::generate();
   const auto signature = p256::sign_der(key, message);

   BOOST_TEST(p256::verify_der(key.get_public_key(), message, signature));
   BOOST_TEST(!p256::verify_der(key.get_public_key(), wrong_message, signature));

   auto malformed = signature;
   malformed.front() ^= 0xffU;
   BOOST_CHECK_THROW((void)p256::verify_der(key.get_public_key(), message, malformed),
                     p256::exceptions::invalid_signature);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_k1_recyle) try {
   auto key = private_key::generate<secp256k1::private_key>();
   auto pub = key.get_public_key();
   auto pub_str = encoding::forge().format(pub);
   auto recycled_pub = encoding::forge().parse_public(pub_str);

   std::cout << encoding::forge().format(pub) << " -> " << encoding::forge().format(recycled_pub) << std::endl;

   BOOST_CHECK(pub == recycled_pub);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_p256_recycle) try {
   auto key = private_key::generate<p256::private_key>();
   auto pub = key.get_public_key();
   auto pub_str = encoding::forge().format(pub);
   auto recycled_pub = encoding::forge().parse_public(pub_str);

   std::cout << encoding::forge().format(pub) << " -> " << encoding::forge().format(recycled_pub) << std::endl;

   BOOST_CHECK(pub == recycled_pub);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(generic_sign_verify_all_supported_algorithms) try {
   const auto message = std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'c', 'r', 'y', 'p', 't', 'o'};
   const auto wrong_message = std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'};
   const auto keys = std::vector<private_key>{
       private_key::generate<secp256k1::private_key>(),
       private_key::generate<p256::private_key>(),
       private_key::generate<ed25519::private_key>(),
       private_key::generate<rsa::private_key>(),
   };

   for (const auto& key : keys) {
      auto sig = key.sign(message);
      auto pub = key.get_public_key();
      BOOST_CHECK(verify(pub, message, sig));
      BOOST_CHECK(!verify(pub, wrong_message, sig));
      BOOST_CHECK_EQUAL(encoding::forge().format(pub).substr(0, 4), "PUB_");
      BOOST_CHECK_EQUAL(encoding::forge().format(sig).substr(0, 4), "SIG_");
   }
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(forge_encoding_roundtrips_all_host_private_keys) try {
   const auto keys = std::vector<private_key>{
       private_key::generate<secp256k1::private_key>(),
       private_key::generate<p256::private_key>(),
       private_key::generate<ed25519::private_key>(),
       private_key::generate<rsa::private_key>(),
   };

   for (const auto& key : keys) {
      const auto text = encoding::forge().format(key);
      BOOST_CHECK(encoding::forge().parse_private(text) == key);
   }
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(generic_sign_digest_all_supported_algorithms) try {
   const auto digest = forge::crypto::digest::sha256::hash(std::string{"forge-crypto-digest"});
   const auto keys = std::vector<private_key>{
       private_key::generate<secp256k1::private_key>(),
       private_key::generate<p256::private_key>(),
       private_key::generate<ed25519::private_key>(),
       private_key::generate<rsa::private_key>(),
   };

   for (const auto& key : keys) {
      const auto sig = key.sign_digest(digest);
      const auto pub = key.get_public_key();
      switch (key.type()) {
      case algorithm::secp256k1:
      case algorithm::p256: {
         const auto recovered = recover(sig, digest, true);
         BOOST_CHECK(recovered == pub);
      } break;
      case algorithm::ed25519:
      case algorithm::rsa:
         BOOST_TEST(verify(pub, digest.to_uint8_span(), sig));
         break;
      case algorithm::webauthn:
         BOOST_FAIL("WebAuthn private keys are not supported");
         break;
      }
   }
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(x25519_key_agreement_roundtrip) try {
   auto alice = x25519::private_key::generate();
   auto bob = x25519::private_key::generate();

   BOOST_CHECK(alice.get_shared_secret(bob.get_public_key()) == bob.get_shared_secret(alice.get_public_key()));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(chacha20_poly1305_authenticates_ciphertext) try {
   auto key = chacha20_poly1305::key{};
   key.fill(7);
   auto nonce = chacha20_poly1305::nonce{};
   nonce.fill(3);
   const auto ad = std::vector<std::uint8_t>{1, 2, 3};
   const auto plaintext = std::vector<std::uint8_t>{4, 5, 6, 7};

   auto encrypted = chacha20_poly1305::encrypt(key, nonce, ad, plaintext);
   auto decrypted = chacha20_poly1305::decrypt(key, nonce, ad, encrypted);
   BOOST_CHECK_EQUAL_COLLECTIONS(plaintext.begin(), plaintext.end(), decrypted.begin(), decrypted.end());

   encrypted.back() ^= 0x01;
   BOOST_CHECK_THROW((void)chacha20_poly1305::decrypt(key, nonce, ad, encrypted), forge::exceptions::base);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()

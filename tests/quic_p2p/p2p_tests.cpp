module;

#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/system_error.hpp>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "libp2p_identity_fixture.hxx"

module forge.net.p2p.node;

import forge.asio.blocking;
import forge.asio.gate;
import forge.asio.runtime;
import forge.api.core.binding;
import forge.api.core.connection;
import forge.api.core.duplex_stream;
import forge.api.core.registry;
import forge.api.p2p.binding;
import forge.api.transport.connection;
import forge.crypto.asymmetric;
import forge.crypto.pki.der;
import forge.crypto.asymmetric.p256;
import forge.crypto.pki.pem;
import forge.crypto.asymmetric.rsa;
import forge.crypto.asymmetric.secp256k1;
import forge.crypto.pki.x509;
import forge.net.p2p.dht;
import forge.net.p2p.dht.record_store;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.envelope;
import forge.net.p2p.exceptions;
import forge.net.p2p.diagnostics;
import forge.net.p2p.hole_punch;
import forge.net.p2p.identify;
import forge.net.p2p.identity;
import forge.net.p2p.ipns;
import forge.net.p2p.message;
import forge.net.p2p.negotiation;
import forge.net.p2p.peer_store;
import forge.net.p2p.protocol;
import forge.net.p2p.pubsub;
import forge.net.p2p.reachability;
import forge.net.p2p.rendezvous;
import forge.net.p2p.relay;
import forge.net.p2p.resource_manager;
import forge.net.p2p.scoring;
import forge.net.p2p.stream;
import forge.net.quic.endpoint;
import forge.net.quic.libp2p;
import forge.net.quic.transport;
import forge.net.stcp.connection;
import forge.net.transport.endpoint;
import forge.net.transport.frame;
import forge.net.transport.stream;
import forge.net.tcp.connection;
import forge.net.tcp.listener;
import forge.multiformats.exceptions;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.multicodec;
import forge.multiformats.multihash;
import forge.multiformats.multibase;
import forge.multiformats.multiaddr;

namespace p2p_live_types {

class live_api : public forge::api::core::contract<live_api, forge::api::core::surface::local |
                                                                 forge::api::core::surface::remote> {
 public:
   virtual ~live_api() = default;

   virtual boost::asio::awaitable<void> exchange(forge::api::core::duplex_stream<std::uint32_t, std::uint32_t>) = 0;
};

} // namespace p2p_live_types

FORGE_API(::p2p_live_types::live_api, FORGE_API_CONTRACT("test.p2p.live", 1, 0), FORGE_API_METHOD(exchange))

#include "../../libraries/net/p2p/details/session_lifecycle.hxx"
#include "../../libraries/net/p2p/details/libp2p_tls.hxx"
#include "../../libraries/net/p2p/details/dht_exchange.hxx"
#include "../../libraries/net/p2p/details/dht_fanout.hxx"
#include "../../libraries/net/p2p/details/dht_query.hxx"
#include "../../libraries/net/p2p/details/relay_budget.hxx"
#include "../../libraries/net/p2p/details/relay_pair.hxx"
#include "../../libraries/net/p2p/details/connection_singleflight_registry.hxx"
#include "../../libraries/net/p2p/details/peer_exchange_learning.hxx"
#include "../../libraries/net/p2p/details/relay_discovery.hxx"
#include "../../libraries/net/p2p/details/pubsub_outbound_budget.hxx"
#include "../../libraries/net/p2p/details/peer_failure.hxx"

namespace forge::net::p2p {
namespace {

using live_api = p2p_live_types::live_api;

class live_impl final : public live_api {
 public:
   boost::asio::awaitable<void>
   exchange(forge::api::core::duplex_stream<std::uint32_t, std::uint32_t> stream) override {
      while (const auto value = co_await stream.async_read()) {
         co_await stream.async_write(*value * 2U);
      }
      co_await stream.async_close();
   }
};

struct product_announce {
   std::string ref;

   bool operator==(const product_announce&) const = default;
};

BOOST_DESCRIBE_STRUCT(product_announce, (), (ref))

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

struct evp_pkey_ctx_deleter {
   void operator()(EVP_PKEY_CTX* value) const noexcept {
      EVP_PKEY_CTX_free(value);
   }
};

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct asn1_object_deleter {
   void operator()(ASN1_OBJECT* value) const noexcept {
      ASN1_OBJECT_free(value);
   }
};

struct asn1_octet_string_deleter {
   void operator()(ASN1_OCTET_STRING* value) const noexcept {
      ASN1_OCTET_STRING_free(value);
   }
};

struct x509_extension_deleter {
   void operator()(X509_EXTENSION* value) const noexcept {
      X509_EXTENSION_free(value);
   }
};

struct test_identity {
   public_key key;
   forge::crypto::asymmetric::private_key private_key;
   std::string private_key_pem;
   peer_id peer;
};

struct test_certificate_identity {
   std::string certificate_pem;
   std::string private_key_pem;
   peer_id peer;
};

std::string bio_to_string(BIO* value) {
   BUF_MEM* buffer = nullptr;
   BIO_get_mem_ptr(value, &buffer);
   if (buffer == nullptr || buffer->data == nullptr) {
      throw std::runtime_error{"failed to read BIO buffer"};
   }
   return {buffer->data, buffer->length};
}

test_identity make_test_identity() {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")};
   if (!key) {
      throw std::runtime_error{"failed to generate Ed25519 identity"};
   }

   auto public_size = std::size_t{};
   if (EVP_PKEY_get_raw_public_key(key.get(), nullptr, &public_size) != 1 || public_size == 0) {
      throw std::runtime_error{"failed to size Ed25519 public key"};
   }
   auto public_bytes = std::vector<std::uint8_t>(public_size);
   if (EVP_PKEY_get_raw_public_key(key.get(), public_bytes.data(), &public_size) != 1) {
      throw std::runtime_error{"failed to read Ed25519 public key"};
   }
   public_bytes.resize(public_size);

   auto private_key_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   if (!private_key_bio ||
       PEM_write_bio_PrivateKey(private_key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      throw std::runtime_error{"failed to write Ed25519 private key PEM"};
   }

   const auto private_key_pem = bio_to_string(private_key_bio.get());
   auto out = test_identity{
       .key = public_key{.type = public_key::type::ed25519, .data = std::move(public_bytes)},
       .private_key = forge::crypto::pki::pem::read_private_key(private_key_pem),
       .private_key_pem = private_key_pem,
   };
   out.peer = make_peer_id(out.key);
   return out;
}

template <typename Range> std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

std::vector<std::uint8_t> certificate_public_key_der(X509* certificate);
std::vector<std::uint8_t> signed_key_der(std::span<const std::uint8_t> public_key,
                                         std::span<const std::uint8_t> signature);
std::vector<std::uint8_t> tls_identity_message(std::span<const std::uint8_t> certificate_public_key);

test_identity make_secp256k1_identity() {
   auto private_key =
       forge::crypto::asymmetric::private_key::generate<forge::crypto::asymmetric::secp256k1::private_key>();
   auto key = public_key{
       .type = public_key::type::secp256k1,
       .data = bytes_from_range(
           std::get<forge::crypto::asymmetric::k1_public_key>(private_key.get_public_key()).serialize()),
   };
   auto out = test_identity{.key = std::move(key), .private_key = private_key};
   out.peer = make_peer_id(out.key);
   return out;
}

test_identity make_p256_identity() {
   auto private_key =
       forge::crypto::asymmetric::private_key::generate_p256<forge::crypto::asymmetric::p256::private_key>();
   auto key = public_key{
       .type = public_key::type::ecdsa,
       .data = forge::crypto::pki::der::write_public_key(private_key.get_public_key()),
   };
   auto out = test_identity{.key = std::move(key), .private_key = private_key};
   out.peer = make_peer_id(out.key);
   return out;
}

test_identity make_rsa_identity() {
   auto private_key = forge::crypto::asymmetric::private_key::generate<forge::crypto::asymmetric::rsa::private_key>();
   auto key = public_key{
       .type = public_key::type::rsa,
       .data = std::get<forge::crypto::asymmetric::rsa_public_key>(private_key.get_public_key()).serialize(),
   };
   auto out = test_identity{.key = std::move(key), .private_key = private_key};
   out.peer = make_peer_id(out.key);
   return out;
}

std::vector<std::uint8_t> make_signed_rendezvous_peer_record(const test_identity& identity,
                                                             std::vector<endpoint> endpoints = {},
                                                             std::uint64_t sequence = 1) {
   if (endpoints.empty()) {
      endpoints.push_back(parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + identity.peer.to_string()));
   }
   return rendezvous::codec::seal_peer_record(
              rendezvous::peer_record{
                  .peer = identity.peer,
                  .endpoints = std::move(endpoints),
                  .sequence = sequence,
              },
              identity.key, forge::crypto::pki::pem::read_private_key(identity.private_key_pem))
       .encode();
}

std::vector<std::uint8_t> make_signed_rendezvous_peer_record(const test_certificate_identity& identity,
                                                             std::vector<endpoint> endpoints = {},
                                                             std::uint64_t sequence = 1) {
   const auto private_key = forge::crypto::pki::pem::read_private_key(identity.private_key_pem);
   const auto key = public_key{
       .type = public_key::type::rsa,
       .data = std::get<forge::crypto::asymmetric::rsa_public_key>(private_key.get_public_key()).serialize(),
   };
   if (endpoints.empty()) {
      endpoints.push_back(parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + identity.peer.to_string()));
   }
   return rendezvous::codec::seal_peer_record(
              rendezvous::peer_record{
                  .peer = identity.peer,
                  .endpoints = std::move(endpoints),
                  .sequence = sequence,
              },
              key, private_key)
       .encode();
}

public_key public_key_for(const test_certificate_identity& identity) {
   const auto private_key = forge::crypto::pki::pem::read_private_key(identity.private_key_pem);
   return public_key{
       .type = public_key::type::rsa,
       .data = std::get<forge::crypto::asymmetric::rsa_public_key>(private_key.get_public_key()).serialize(),
   };
}

[[nodiscard]] std::vector<std::uint8_t> identify_peer_record_payload_type() {
   return {0x03, 0x01};
}

std::vector<std::uint8_t> make_signed_identify_peer_record(const test_identity& identity,
                                                           std::vector<endpoint> endpoints = {},
                                                           std::uint64_t sequence = 1) {
   if (endpoints.empty()) {
      endpoints.push_back(parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + identity.peer.to_string()));
   }
   const auto payload = rendezvous::codec::encode_peer_record(rendezvous::peer_record{
       .peer = identity.peer,
       .endpoints = std::move(endpoints),
       .sequence = sequence,
   });
   return signed_envelope::seal(identity.key, forge::crypto::pki::pem::read_private_key(identity.private_key_pem),
                                "libp2p-peer-record", identify_peer_record_payload_type(), payload)
       .encode();
}

std::vector<std::uint8_t> make_signed_identify_peer_record(const test_certificate_identity& identity,
                                                           std::vector<endpoint> endpoints = {},
                                                           std::uint64_t sequence = 1) {
   if (endpoints.empty()) {
      endpoints.push_back(parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + identity.peer.to_string()));
   }
   const auto payload = rendezvous::codec::encode_peer_record(rendezvous::peer_record{
       .peer = identity.peer,
       .endpoints = std::move(endpoints),
       .sequence = sequence,
   });
   return signed_envelope::seal(public_key_for(identity),
                                forge::crypto::pki::pem::read_private_key(identity.private_key_pem),
                                "libp2p-peer-record", identify_peer_record_payload_type(), payload)
       .encode();
}

[[nodiscard]] std::uint64_t identify_peer_record_sequence(std::span<const std::uint8_t> bytes) {
   return rendezvous::codec::decode_peer_record(signed_envelope::decode(bytes).payload).sequence;
}

test_certificate_identity make_test_certificate_identity(std::string_view common_name) {
   auto fixture = forge::tests::p2p::make_identity_fixture(common_name);
   auto out = test_certificate_identity{
       .certificate_pem = std::move(fixture.certificate_pem),
       .private_key_pem = std::move(fixture.private_key_pem),
   };
   out.peer = make_peer_id_from_certificate_pem(out.certificate_pem);
   return out;
}

std::string_view test_certificate() {
   return "-----BEGIN CERTIFICATE-----\n"
          "MIICpDCCAYwCCQCJjaEDxrQqBzANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
          "MjcuMC4wLjEwHhcNMjYwNDI5MDgwMTMzWhcNMjYwNDMwMDgwMTMzWjAUMRIwEAYD\n"
          "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDy\n"
          "sbPH/R4QUz725sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TX\n"
          "gl9tHkNpKmI92s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+\n"
          "x7MRWXfKYd/ArGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOS\n"
          "lI/lDqIjZxo7jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuv\n"
          "M+mTj6eO4UQ42w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXj\n"
          "nPOZzBinLRTDnE59HbDZAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHSOUQTEDgjC\n"
          "uwza9ayfThJTs43j+TziWHLlowqCiHt/ipRNFEW7L0ibTnbMdQBFGfaLkTAhc5Rd\n"
          "6O6x+9o76pgEYxEg0rDkgNXmprNmS+nL7Are+iiF6R+X8dts3MQgtONPApAXE96P\n"
          "/n5K4GDQTd3WCI37hkmJA6rmwziFDTlwqtKWts39g8PqAbXac27rVR/iD0gWdOws\n"
          "qiaoGj/0WW9qcgjYGdCc0/CbbnyiWbi48VVf0yyfm7wgcz90byaKIQchHdb/qjyU\n"
          "wy7nfU5TJ5MKQ5yeqPTWmPYZZp9TKa5VD6wZD/IH7jH3GdJ/fSyroVLZktVnmxJa\n"
          "dmG/9wwivwQ=\n"
          "-----END CERTIFICATE-----\n";
}

std::string_view test_private_key() {
   return "-----BEGIN PRIVATE KEY-----\n"
          "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDysbPH/R4QUz72\n"
          "5sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TXgl9tHkNpKmI9\n"
          "2s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+x7MRWXfKYd/A\n"
          "rGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOSlI/lDqIjZxo7\n"
          "jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuvM+mTj6eO4UQ4\n"
          "2w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXjnPOZzBinLRTD\n"
          "nE59HbDZAgMBAAECggEBAIWVjHhy+V5RA+JRCh/12ayirNLG2BF30OP9pf7iL4IT\n"
          "/dMPbKvkmDGLw+1bW8tgKXj5+N6N/trfCm4zhqI3OF7ihooH9qYM88/F/OvMjFiU\n"
          "BhMVVhJW1LxtPPjKUcFN58M8VnMhRM9v6gIaoSOJZvpU1abVtgBDocyJUxAB6gYp\n"
          "i7MzoRwHGsL5mW/luE5H92/S8NNwLWBDA7DIGfrTZ6POf92h5I5W3CuTcqR5FICz\n"
          "3pfU3i443yZmsmkc9duH2gZ9cb9j4pRtNLbbsGmRVrBlgnkVFk8JWbikc8MpLeKO\n"
          "VKP7A2NvxJIrc7oFYrf4hbw8P70YL7S9B3W3yBPPzJECgYEA+Y3nG8CtvVTE/Keo\n"
          "qb5Rljlnj9DEffrylLyYUYfSSNR4Olc2WCPBiz0rPCDdO0VGeXAwqLf2VP7IEyAx\n"
          "kvrnqhzHWMhiLv+k4tIVyKCwpuofN0JsoUCi7CwRf+H2Pg+t6ewLV116THKsd41H\n"
          "IRElWyEvZsmbbhlLrsxUtfFZWnUCgYEA+PZwXUn+cb8kRmfG959gMawTtcfvnBUX\n"
          "sIn7LQl/ZWUIiLMWCaS3FbqkiGjaEYo6om1invYNJNA9zp/ECauSDp58NICCL0ie\n"
          "L7z26sEa6Ocg2VdR4ezpN3cM6dyAKfTFGb9V6qjyqNIPCE4eey6ZJ+CU/mpEfSDu\n"
          "+RGMzfdDCFUCgYEA5FRUn0zk6jU0YyMXq+9pgLSXL7vI/Kdt6m7AQuCto1tbga2o\n"
          "GG7mt/pIo6RCJufUemoO62AeL1hKQU2UbjHJYxkfv/jf9LaM68dijQWRe7b8xres\n"
          "4sFcEBCmFkbt4YzBCCWjntT1gBrv+Ba4fOXOMxoi374Yy1yzpYRpAWuI4L0CgYAn\n"
          "u1SlXrivuHx2i/tR62pzou2mVhkkRK16LBsczeY57UzWXBZJRbM+UYIOjwU2RWQk\n"
          "JebWTZg9ZspmXlLv5CS0FpDl5BhiqWktXy/cuSKtRq2UYf4cWy3A/0vdSqZdi8Wk\n"
          "3Uc94uaPEK77eVQd/orMtWexzo3NlmLs9uMMv8g/3QKBgQCbik0UoJkkqNRMmWG8\n"
          "dKQzj58eRI8fmKdJlWNfj2QMspd2vXMbsWYgAbFbU1QcVs1n8PxNydM+cfy77w8q\n"
          "NWMlYP7rUFQ3ekYWqrRlshZdJ/h24PALd1nPCvhc4C9dvn+zW3BLVez1lBuFO8n8\n"
          "0YkgmTgW7Ieibqnf4DqYp//nkw==\n"
          "-----END PRIVATE KEY-----\n";
}

peer_id legacy_cert_hash_peer_id(std::string_view certificate_pem) {
   const auto certificate = forge::crypto::pki::x509::certificate::from_pem(certificate_pem);
   const auto der = certificate.der();
   return peer_id::from_bytes(forge::multiformats::multihash::sha2_256(der).encode());
}

peer_id peer(std::uint8_t value) {
   const auto payload = forge::multiformats::bytes{value};
   return peer_id::from_bytes(forge::multiformats::multihash::identity(payload).encode());
}

std::uint8_t hex_value(char value) {
   if (value >= '0' && value <= '9') {
      return static_cast<std::uint8_t>(value - '0');
   }
   if (value >= 'a' && value <= 'f') {
      return static_cast<std::uint8_t>(10 + value - 'a');
   }
   if (value >= 'A' && value <= 'F') {
      return static_cast<std::uint8_t>(10 + value - 'A');
   }
   throw std::runtime_error{"bad hex"};
}

std::vector<std::uint8_t> bytes_from_hex(std::string_view hex) {
   if ((hex.size() % 2) != 0) {
      throw std::runtime_error{"odd hex"};
   }
   auto out = std::vector<std::uint8_t>{};
   out.reserve(hex.size() / 2);
   for (std::size_t i = 0; i < hex.size(); i += 2) {
      out.push_back(static_cast<std::uint8_t>((hex_value(hex[i]) << 4U) | hex_value(hex[i + 1])));
   }
   return out;
}

void append_der_length(std::vector<std::uint8_t>& out, std::size_t value) {
   if (value < 128) {
      out.push_back(static_cast<std::uint8_t>(value));
      return;
   }
   auto bytes = std::vector<std::uint8_t>{};
   while (value != 0) {
      bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
      value >>= 8U;
   }
   out.push_back(static_cast<std::uint8_t>(0x80U | bytes.size()));
   for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
      out.push_back(*it);
   }
}

void append_der_octet_string(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> value) {
   out.push_back(0x04);
   append_der_length(out, value.size());
   out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> signed_key_der(std::span<const std::uint8_t> public_key,
                                         std::span<const std::uint8_t> signature) {
   auto content = std::vector<std::uint8_t>{};
   append_der_octet_string(content, public_key);
   append_der_octet_string(content, signature);
   auto out = std::vector<std::uint8_t>{0x30};
   append_der_length(out, content.size());
   out.insert(out.end(), content.begin(), content.end());
   return out;
}

std::vector<std::uint8_t> signed_key_der_with_overflowing_octet_length() {
   auto out = std::vector<std::uint8_t>{0x30, 0x0a, 0x04, 0x88};
   out.insert(out.end(), 8, 0xff);
   return out;
}

std::vector<std::uint8_t> certificate_public_key_der(X509* certificate) {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{X509_get_pubkey(certificate)};
   if (!key) {
      throw std::runtime_error{"failed to get certificate public key"};
   }
   const auto length = i2d_PUBKEY(key.get(), nullptr);
   if (length <= 0) {
      throw std::runtime_error{"failed to size certificate public key DER"};
   }
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key.get(), &cursor) != length) {
      throw std::runtime_error{"failed to write certificate public key DER"};
   }
   return out;
}

template <typename ExtensionFactory>
std::vector<std::uint8_t> make_certificate_der_with_libp2p_extension_from_factory(ExtensionFactory make_extension) {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")};
   if (!key) {
      throw std::runtime_error{"failed to generate certificate key"};
   }
   auto certificate = std::unique_ptr<X509, x509_deleter>{X509_new()};
   if (!certificate || X509_set_version(certificate.get(), 2) != 1 ||
       ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 77) != 1 ||
       X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) == nullptr ||
       X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 24 * 60 * 60) == nullptr ||
       X509_set_pubkey(certificate.get(), key.get()) != 1) {
      throw std::runtime_error{"failed to configure certificate"};
   }
   auto* name = X509_get_subject_name(certificate.get());
   const auto common_name = std::string_view{"forge-libp2p-identity-test"};
   if (name == nullptr ||
       X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name.data()),
                                  static_cast<int>(common_name.size()), -1, 0) != 1 ||
       X509_set_issuer_name(certificate.get(), name) != 1) {
      throw std::runtime_error{"failed to configure certificate subject"};
   }
   const auto public_key_der = certificate_public_key_der(certificate.get());
   const auto extension_value = make_extension(std::span<const std::uint8_t>{public_key_der});
   auto object = std::unique_ptr<ASN1_OBJECT, asn1_object_deleter>{OBJ_txt2obj("1.3.6.1.4.1.53594.1.1", 1)};
   auto octets = std::unique_ptr<ASN1_OCTET_STRING, asn1_octet_string_deleter>{ASN1_OCTET_STRING_new()};
   if (!object || !octets ||
       ASN1_OCTET_STRING_set(octets.get(), extension_value.data(), static_cast<int>(extension_value.size())) != 1) {
      throw std::runtime_error{"failed to create certificate extension value"};
   }
   auto extension = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>{
       X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 1, octets.get())};
   if (!extension || X509_add_ext(certificate.get(), extension.get(), -1) != 1 ||
       X509_sign(certificate.get(), key.get(), nullptr) <= 0) {
      throw std::runtime_error{"failed to sign certificate"};
   }
   const auto length = i2d_X509(certificate.get(), nullptr);
   if (length <= 0) {
      throw std::runtime_error{"failed to size certificate DER"};
   }
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_X509(certificate.get(), &cursor) != length) {
      throw std::runtime_error{"failed to write certificate DER"};
   }
   return out;
}

std::vector<std::uint8_t> make_certificate_der_with_libp2p_extension(std::span<const std::uint8_t> extension_value) {
   return make_certificate_der_with_libp2p_extension_from_factory([&](std::span<const std::uint8_t>) {
      return std::vector<std::uint8_t>{extension_value.begin(), extension_value.end()};
   });
}

std::vector<std::uint8_t> tls_identity_message(std::span<const std::uint8_t> certificate_public_key) {
   auto out = std::vector<std::uint8_t>{};
   constexpr auto prefix = std::string_view{"libp2p-tls-handshake:"};
   out.insert(out.end(), prefix.begin(), prefix.end());
   out.insert(out.end(), certificate_public_key.begin(), certificate_public_key.end());
   return out;
}

std::vector<std::uint8_t> sign_test_identity(const test_identity& identity, std::span<const std::uint8_t> message) {
   return identity.private_key.visit([&](const auto& key) -> std::vector<std::uint8_t> {
      using key_type = std::decay_t<decltype(key)>;
      if constexpr (std::is_same_v<key_type, forge::crypto::asymmetric::secp256k1::private_key>) {
         return forge::crypto::asymmetric::secp256k1::sign_der(key, message);
      } else if constexpr (std::is_same_v<key_type, forge::crypto::asymmetric::p256::private_key>) {
         return forge::crypto::asymmetric::p256::sign_der(key, message);
      } else {
         return bytes_from_range(key.sign(message));
      }
   });
}

std::vector<std::uint8_t> signed_tls_extension(const test_identity& identity,
                                               std::span<const std::uint8_t> certificate_public_key) {
   const auto message = tls_identity_message(certificate_public_key);
   const auto signature = sign_test_identity(identity, message);
   return signed_key_der(encode_public_key(identity.key), signature);
}

const auto content_swarm_test_dht = protocol_id{.value = "/forge/test/content-swarm/kad/1.0.0"};
const auto content_swarm_value_test_dht = protocol_id{.value = "/forge/test/content-swarm/value-kad/1.0.0"};

[[nodiscard]] dht::profile custom_test_dht_profile(dht::mode operating_mode = dht::mode::client,
                                                   dht::options limits = {}) {
   return custom_dht_profile(content_swarm_test_dht, operating_mode,
                             dht::profile_capabilities{.peers = true, .providers = true, .values = false}, {},
                             std::move(limits));
}

[[nodiscard]] dht::profile custom_test_value_dht_profile(dht::mode operating_mode = dht::mode::client,
                                                         dht::options limits = {}) {
   return custom_dht_profile(
       content_swarm_value_test_dht, operating_mode,
       dht::profile_capabilities{.peers = true, .providers = false, .values = true},
       {dht::value_policy{
           .key_prefix = {'/'},
           .validate =
               [](const dht::record& value, dht::value_validation_context) {
                  if (value.value.empty()) {
                     throw std::runtime_error{"empty test DHT value"};
                  }
               },
           .select = [](std::span<const dht::record>) { return std::size_t{}; },
           .expiry = [](const dht::record&, dht::value_expiry_context context) { return context.supplied_expires_at; },
       }},
       std::move(limits));
}

[[nodiscard]] dht::key amino_provider_key(std::span<const std::uint8_t> value) {
   const auto multihash = forge::multiformats::multihash::sha2_256(value).encode();
   return make_dht_key(multihash);
}

node::options options_for(peer_id id, capability_set capabilities = capability_set{
                                          .bits = capabilities::direct_quic | capabilities::peer_exchange}) {
   return node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = std::move(id),
       .capabilities = capabilities,
       .allow_insecure_test_mode = true,
   };
}

node::options options_for(const test_certificate_identity& identity,
                          capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                               capabilities::peer_exchange}) {
   return node::options{
       .certificate_pem = identity.certificate_pem,
       .private_key_pem = identity.private_key_pem,
       .capabilities = capabilities,
       .allow_insecure_test_mode = true,
   };
}

node::options options_for(const test_identity& identity,
                          capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                               capabilities::peer_exchange}) {
   auto out = options_for(identity.peer, capabilities);
   out.private_key_pem = identity.private_key_pem;
   out.public_key = encode_public_key(identity.key);
   return out;
}

node::options dht_options_for(peer_id id, dht::profile profile,
                              capability_set capabilities = capability_set{.bits = capabilities::direct_quic}) {
   auto out = options_for(std::move(id), capabilities);
   out.dht_profiles.push_back(std::move(profile));
   return out;
}

node::options dht_options_for(const test_certificate_identity& identity, dht::profile profile,
                              capability_set capabilities = capability_set{.bits = capabilities::direct_quic}) {
   auto out = options_for(identity, capabilities);
   out.dht_profiles.push_back(std::move(profile));
   return out;
}

node::options dht_options_for(const test_identity& identity, dht::profile profile,
                              capability_set capabilities = capability_set{.bits = capabilities::direct_quic}) {
   auto out = options_for(identity, capabilities);
   out.dht_profiles.push_back(std::move(profile));
   return out;
}

public_key test_rsa_public_key() {
   return public_key{
       .type = public_key::type::rsa,
       .data = forge::crypto::pki::der::write_public_key(
           forge::crypto::pki::pem::read_private_key(test_private_key()).get_public_key()),
   };
}

node::options pubsub_options_for(capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                                      capabilities::pubsub}) {
   const auto key = test_rsa_public_key();
   auto out = options_for(make_peer_id(key), capabilities);
   out.public_key = encode_public_key(key);
   return out;
}

node::options pubsub_options_for(const test_certificate_identity& identity,
                                 capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                                      capabilities::pubsub}) {
   return options_for(identity, capabilities);
}

void register_echo(node& value) {
   value.register_protocol_handler(builtins::echo,
                                   [](node::incoming_protocol_stream incoming) mutable -> boost::asio::awaitable<void> {
                                      auto payload = co_await incoming.stream.async_read_frame();
                                      co_await incoming.stream.async_write_frame(payload);
                                   });
}

void register_echo(node& value, protocol_id protocol) {
   value.register_protocol_handler(std::move(protocol),
                                   [](node::incoming_protocol_stream incoming) mutable -> boost::asio::awaitable<void> {
                                      auto payload = co_await incoming.stream.async_read_frame();
                                      co_await incoming.stream.async_write_frame(payload);
                                   });
}

[[nodiscard]] endpoint make_quic_endpoint(std::uint16_t port, std::string host = "127.0.0.1") {
   return endpoint{.transport = {.host_type = endpoint::host_kind::ip4,
                                 .protocol = endpoint::protocol_kind::quic_v1,
                                 .host = std::move(host),
                                 .port = port}};
}

[[nodiscard]] endpoint make_tcp_endpoint(std::uint16_t port, std::string host = "127.0.0.1") {
   return endpoint{.transport = {.host_type = endpoint::host_kind::ip4,
                                 .protocol = endpoint::protocol_kind::tcp,
                                 .host = std::move(host),
                                 .port = port}};
}

[[nodiscard]] endpoint make_dns_tcp_endpoint(std::uint16_t port, std::string host) {
   return endpoint{.transport = {.host_type = endpoint::host_kind::dns,
                                 .protocol = endpoint::protocol_kind::tcp,
                                 .host = std::move(host),
                                 .port = port}};
}

endpoint listen(node& value, forge::asio::runtime& runtime) {
   forge::asio::blocking::run(runtime, value.async_listen(make_quic_endpoint(0)));
   auto endpoint = value.local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   return *endpoint;
}

endpoint listen_tcp(node& value, forge::asio::runtime& runtime) {
   forge::asio::blocking::run(runtime, value.async_listen(make_tcp_endpoint(0)));
   auto endpoint = value.local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   return *endpoint;
}

void wait_on_runtime(forge::asio::runtime& runtime, std::chrono::milliseconds delay, std::string_view label);

void verify_dht_server(forge::asio::runtime& runtime, node& client, const node& server, const endpoint& server_endpoint,
                       const protocol_id& protocol, const std::optional<endpoint>& advertised_endpoint = std::nullopt) {
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   if (advertised_endpoint) {
      client.peers().learn_endpoint(server.local_peer(), *advertised_endpoint,
                                    capability_set{.bits = capabilities::direct_quic});
   }
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   for (auto attempt = 0U; attempt < 20U && client.routing_status(protocol).active == 0U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "DHT Identify admission");
   }
   if (client.routing_status(protocol).active == 0U) {
      if (const auto record = client.peers().find(server.local_peer())) {
         BOOST_TEST_MESSAGE("DHT peer record protocols=" << record->protocols.size());
         for (const auto& current : record->protocols) {
            BOOST_TEST_MESSAGE("DHT peer protocol=" << current.value);
         }
      }
      for (const auto& session : client.diagnostics().sessions) {
         BOOST_TEST_MESSAGE("DHT session identify_state=" << static_cast<int>(session.identify_state)
                                                          << " error=" << session.identify_error);
      }
   }
   BOOST_REQUIRE(client.routing_status(protocol).active > 0U);
}

boost::asio::awaitable<void> exercise_live_api(forge::api::transport::connection& connection) {
   auto remote = co_await connection.get_remote_api<live_api>();
   auto call = co_await remote.async_open<&live_api::exchange>();
   co_await call.async_write(3U);
   auto first = co_await call.async_read();
   BOOST_REQUIRE(first.has_value());
   BOOST_TEST(*first == 6U);
   co_await call.async_write(5U);
   auto second = co_await call.async_read();
   BOOST_REQUIRE(second.has_value());
   BOOST_TEST(*second == 10U);
   co_await call.async_close();
   BOOST_TEST(!(co_await call.async_read()).has_value());
   co_await call.async_finish();
   co_await connection.async_close();
}

void run_live_api_over(endpoint::protocol_kind transport) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(peer(242));
   auto client_options = options_for(peer(241));
   if (transport == endpoint::protocol_kind::tcp) {
      server_options = options_for(make_test_identity());
      client_options = options_for(make_test_identity());
   }
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   auto implementation = std::make_shared<live_impl>();
   auto registry = forge::api::core::registry{};
   registry.install<live_api>(live_api::describe(), implementation);
   auto binding = forge::api::p2p::api(server).use(forge::api::core::binding().serve(registry).build()).build();
   BOOST_TEST(binding.protocol().value == "/forge/api/2");
   server.register_protocol_handler(binding.protocol(), binding.handler());

   const auto server_endpoint =
       transport == endpoint::protocol_kind::quic_v1 ? listen(server, runtime) : listen_tcp(server, runtime);
   const auto session = forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{
                                                          .expected_peer = server.local_peer(),
                                                          .allow_relay = false,
                                                      }));
   BOOST_TEST(session.remote_peer.value == server.local_peer().value);
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), binding.protocol(),
                                                                             node::open_options{.allow_relay = false}));
   auto connection = forge::api::transport::connection{std::move(stream).into_transport_stream(), binding.options()};
   forge::asio::blocking::run(runtime, exercise_live_api(connection));

   const auto diagnostics = client.diagnostics();
   const auto found = std::ranges::find_if(diagnostics.sessions, [&](const auto& value) {
      return value.remote_peer == server.local_peer() && value.direct_endpoint.has_value();
   });
   BOOST_REQUIRE(found != diagnostics.sessions.end());
   BOOST_TEST(static_cast<int>(found->direct_endpoint->transport.protocol) == static_cast<int>(transport));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

[[nodiscard]] bool contains_protocol(const std::vector<endpoint>& endpoints, endpoint::protocol_kind protocol) {
   return std::ranges::any_of(endpoints,
                              [protocol](const endpoint& value) { return value.transport.protocol == protocol; });
}

[[nodiscard]] endpoint require_endpoint_for(const std::vector<endpoint>& endpoints, endpoint::protocol_kind protocol) {
   auto found = std::ranges::find_if(
       endpoints, [protocol](const endpoint& value) { return value.transport.protocol == protocol; });
   BOOST_REQUIRE(found != endpoints.end());
   return *found;
}

endpoint start_stalling_tcp_peer(forge::asio::runtime& runtime,
                                 std::chrono::milliseconds hold = std::chrono::seconds{2},
                                 std::shared_ptr<std::promise<void>> accepted = {}) {
   namespace asio = boost::asio;
   using asio_tcp = asio::ip::tcp;
   auto acceptor = std::make_shared<asio_tcp::acceptor>(runtime.context(), asio_tcp::endpoint{asio_tcp::v4(), 0});
   auto socket = std::make_shared<asio_tcp::socket>(runtime.context());
   const auto port = acceptor->local_endpoint().port();

   asio::co_spawn(
       runtime.context(),
       [acceptor, socket, hold, accepted = std::move(accepted)]() -> asio::awaitable<void> {
          auto error = boost::system::error_code{};
          co_await acceptor->async_accept(*socket, asio::redirect_error(asio::use_awaitable, error));
          if (!error) {
             if (accepted) {
                accepted->set_value();
             }
             auto timer = asio::steady_timer{co_await asio::this_coro::executor};
             timer.expires_after(hold);
             co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
          }
          auto ignored = boost::system::error_code{};
          socket->close(ignored);
          acceptor->close(ignored);
       },
       asio::detached);

   return make_tcp_endpoint(port);
}

boost::asio::awaitable<std::vector<std::uint8_t>> read_raw_multistream_frame(boost::asio::ip::tcp::socket& socket) {
   auto size = std::uint64_t{};
   auto shift = unsigned{};
   while (shift < 64U) {
      auto byte = std::array<std::uint8_t, 1>{};
      co_await boost::asio::async_read(socket, boost::asio::buffer(byte), boost::asio::use_awaitable);
      size |= static_cast<std::uint64_t>(byte.front() & 0x7fU) << shift;
      if ((byte.front() & 0x80U) == 0) {
         if (size > 16U * 1024U) {
            throw std::runtime_error{"raw multistream frame is too large"};
         }
         auto payload = std::vector<std::uint8_t>(static_cast<std::size_t>(size));
         co_await boost::asio::async_read(socket, boost::asio::buffer(payload), boost::asio::use_awaitable);
         co_return payload;
      }
      shift += 7U;
   }
   throw std::runtime_error{"raw multistream frame has an invalid length"};
}

boost::asio::awaitable<void> write_raw_multistream_message(boost::asio::ip::tcp::socket& socket,
                                                           const protocol_negotiation::message& message) {
   const auto frame = protocol_negotiation::encode_frame(protocol_negotiation::encode_message(message));
   co_await boost::asio::async_write(socket, boost::asio::buffer(frame), boost::asio::use_awaitable);
}

endpoint start_stalling_security_tcp_peer(forge::asio::runtime& runtime, protocol_id selected_protocol,
                                          const std::shared_ptr<std::promise<void>>& security_started) {
   namespace asio = boost::asio;
   using asio_tcp = asio::ip::tcp;
   auto acceptor = std::make_shared<asio_tcp::acceptor>(runtime.context(), asio_tcp::endpoint{asio_tcp::v4(), 0});
   auto socket = std::make_shared<asio_tcp::socket>(runtime.context());
   const auto port = acceptor->local_endpoint().port();

   asio::co_spawn(
       runtime.context(),
       [acceptor, socket, selected_protocol = std::move(selected_protocol),
        security_started]() -> asio::awaitable<void> {
          auto reported = false;
          try {
             co_await acceptor->async_accept(*socket, asio::use_awaitable);
             auto ignored = boost::system::error_code{};
             acceptor->close(ignored);

             const auto header = protocol_negotiation::decode_message(co_await read_raw_multistream_frame(*socket));
             if (header.kind != protocol_negotiation::message_kind::header) {
                throw std::runtime_error{"raw peer expected multistream header"};
             }
             co_await write_raw_multistream_message(
                 *socket, protocol_negotiation::message{.kind = protocol_negotiation::message_kind::header,
                                                        .protocol = protocol_negotiation::multistream_v1});

             while (true) {
                const auto proposal =
                    protocol_negotiation::decode_message(co_await read_raw_multistream_frame(*socket));
                if (proposal.kind != protocol_negotiation::message_kind::protocol) {
                   throw std::runtime_error{"raw peer expected security proposal"};
                }
                if (proposal.protocol.value == selected_protocol.value) {
                   co_await write_raw_multistream_message(*socket, proposal);
                   break;
                }
                co_await write_raw_multistream_message(
                    *socket, protocol_negotiation::message{.kind = protocol_negotiation::message_kind::not_available,
                                                           .protocol = protocol_negotiation::not_available});
             }

             auto byte = std::array<std::uint8_t, 1>{};
             co_await asio::async_read(*socket, asio::buffer(byte), asio::use_awaitable);
             security_started->set_value();
             reported = true;
             while (co_await socket->async_read_some(asio::buffer(byte), asio::use_awaitable)) {
             }
          } catch (...) {
             if (!reported) {
                security_started->set_exception(std::current_exception());
             }
          }
          auto ignored = boost::system::error_code{};
          socket->close(ignored);
          acceptor->close(ignored);
       },
       asio::detached);

   return make_tcp_endpoint(port);
}

endpoint start_stalling_tls_yamux_tcp_peer(forge::asio::runtime& runtime, const node::options& identity_options,
                                           const std::shared_ptr<std::promise<void>>& yamux_started,
                                           const std::shared_ptr<std::promise<void>>& peer_finished) {
   namespace asio = boost::asio;
   using asio_tcp = asio::ip::tcp;
   auto acceptor = std::make_shared<asio_tcp::acceptor>(runtime.context(), asio_tcp::endpoint{asio_tcp::v4(), 0});
   auto socket = std::make_shared<asio_tcp::socket>(runtime.context());
   auto identity = std::make_shared<libp2p_identity_material>(make_libp2p_identity_material(identity_options));
   const auto port = acceptor->local_endpoint().port();

   asio::co_spawn(
       runtime.context(),
       [acceptor, socket, identity = std::move(identity), yamux_started, peer_finished]() -> asio::awaitable<void> {
          auto reported_yamux = false;
          try {
             co_await acceptor->async_accept(*socket, asio::use_awaitable);
             auto ignored = boost::system::error_code{};
             acceptor->close(ignored);

             const auto header = protocol_negotiation::decode_message(co_await read_raw_multistream_frame(*socket));
             if (header.kind != protocol_negotiation::message_kind::header) {
                throw std::runtime_error{"TLS Yamux peer expected multistream header"};
             }
             co_await write_raw_multistream_message(
                 *socket, protocol_negotiation::message{.kind = protocol_negotiation::message_kind::header,
                                                        .protocol = protocol_negotiation::multistream_v1});

             const auto tls_protocol = protocol_id{.value = "/tls/1.0.0"};
             while (true) {
                const auto proposal =
                    protocol_negotiation::decode_message(co_await read_raw_multistream_frame(*socket));
                if (proposal.kind != protocol_negotiation::message_kind::protocol) {
                   throw std::runtime_error{"TLS Yamux peer expected security proposal"};
                }
                if (proposal.protocol.value == tls_protocol.value) {
                   co_await write_raw_multistream_message(*socket, proposal);
                   break;
                }
                co_await write_raw_multistream_message(
                    *socket, protocol_negotiation::message{.kind = protocol_negotiation::message_kind::not_available,
                                                           .protocol = protocol_negotiation::not_available});
             }

             auto tls_options = make_libp2p_tls_server_options(*identity);
             tls_options.alpn_protocols = {"libp2p"};
             auto tls = co_await forge::net::stcp::async_upgrade_server(
                 forge::net::tcp::connection{std::move(*socket)}, std::move(tls_options), std::chrono::seconds{2});
             if (tls.selected_alpn() != "libp2p") {
                throw std::runtime_error{"TLS Yamux peer did not negotiate legacy libp2p ALPN"};
             }

             auto first_yamux_byte = std::array<std::uint8_t, 1>{};
             if (co_await tls.async_read_some(first_yamux_byte) != first_yamux_byte.size()) {
                throw std::runtime_error{"TLS Yamux peer did not receive Yamux negotiation"};
             }
             yamux_started->set_value();
             reported_yamux = true;
             while (true) {
                static_cast<void>(co_await tls.async_read());
             }
          } catch (...) {
             if (!reported_yamux) {
                yamux_started->set_exception(std::current_exception());
             }
          }
          auto ignored = boost::system::error_code{};
          socket->close(ignored);
          acceptor->close(ignored);
          peer_finished->set_value();
       },
       asio::detached);

   return make_tcp_endpoint(port);
}

void wait_for_server(std::future<void>& future, std::chrono::milliseconds timeout, std::string_view label) {
   if (future.wait_for(timeout) != std::future_status::ready) {
      throw std::runtime_error{std::string{label} + " did not finish"};
   }
   future.get();
}

void wait_on_runtime(forge::asio::runtime& runtime, std::chrono::milliseconds delay, std::string_view label) {
   auto future = boost::asio::co_spawn(
       runtime.context(),
       [delay]() -> boost::asio::awaitable<void> {
          auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
          timer.expires_after(delay);
          boost::system::error_code ec;
          co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
       },
       boost::asio::use_future);
   wait_for_server(future, delay + std::chrono::milliseconds{1'000}, label);
}

template <typename Predicate>
peer_store::record wait_for_peer_record(node& value, const peer_id& peer, forge::asio::runtime& runtime,
                                        std::string_view label, Predicate predicate) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
   while (std::chrono::steady_clock::now() < deadline) {
      auto record = value.peers().find(peer);
      if (record && predicate(*record)) {
         return *record;
      }
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, label);
   }
   throw std::runtime_error{std::string{label} + " did not produce the expected peer record"};
}

peer_store::record wait_for_identified_peer(node& value, const peer_id& peer, forge::asio::runtime& runtime,
                                            std::string_view label) {
   return wait_for_peer_record(value, peer, runtime, label,
                               [](const auto& record) { return !record.signed_peer_record.empty(); });
}

std::shared_ptr<std::promise<void>> block_runtime(forge::asio::runtime& runtime, std::string_view label) {
   auto entered = std::make_shared<std::promise<void>>();
   auto entered_future = entered->get_future();
   auto release = std::make_shared<std::promise<void>>();
   auto released = release->get_future().share();
   boost::asio::post(runtime.context(), [entered, released = std::move(released)] {
      entered->set_value();
      released.wait();
   });
   wait_for_server(entered_future, std::chrono::seconds{2}, label);
   return release;
}

std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload) {
   auto out = forge::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes, std::size_t max_payload_size) {
   const auto decoded = forge::multiformats::varint_decode(bytes);
   BOOST_REQUIRE(decoded.value <= max_payload_size);
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   BOOST_REQUIRE_EQUAL(total, bytes.size());
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

boost::asio::awaitable<std::vector<std::uint8_t>>
read_length_delimited(stream& value, std::size_t max_payload_size = 4 * 1024 * 1024) {
   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      try {
         const auto decoded = forge::multiformats::varint_decode(buffer);
         BOOST_REQUIRE(decoded.value <= max_payload_size);
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto frame = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            co_return unwrap_length_delimited(frame, max_payload_size);
         }
      } catch (const forge::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            throw;
         }
      }
      auto chunk = co_await value.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

dht::message exchange_find_node(forge::asio::runtime& runtime, node& client, const peer_id& server,
                                const dht::profile& profile, dht::key key) {
   auto stream = forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server, profile.protocol));
   forge::asio::blocking::run(runtime, stream.async_write(dht::codec::encode(
                                           dht::message{
                                               .type = dht::message_type::find_node,
                                               .key_value = std::move(key),
                                           },
                                           profile)));
   return dht::codec::decode(wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))),
                             profile);
}

class queued_transport_stream final : public forge::net::transport::detail::stream_concept {
 public:
   explicit queued_transport_stream(std::int64_t stream_id) : stream_id_{stream_id} {}

   [[nodiscard]] bool valid() const noexcept override {
      return true;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return stream_id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      writes.push_back({bytes.begin(), bytes.end()});
      co_return;
   }

   boost::asio::awaitable<void> async_write_chunk(forge::net::transport::chunk bytes) override {
      ++chunk_writes;
      writes.push_back(bytes.to_vector());
      co_return;
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      BOOST_REQUIRE(!reads.empty());
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return out;
   }

   boost::asio::awaitable<forge::net::transport::chunk> async_read_chunk() override {
      ++chunk_reads;
      BOOST_REQUIRE(!reads.empty());
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return forge::net::transport::chunk{std::move(out)};
   }

   boost::asio::awaitable<void> async_close() override {
      closed = true;
      co_return;
   }

   void cancel() override {
      closed = true;
   }

   std::deque<std::vector<std::uint8_t>> reads;
   std::vector<std::vector<std::uint8_t>> writes;
   std::size_t chunk_reads = 0;
   std::size_t chunk_writes = 0;
   bool closed = false;

 private:
   std::int64_t stream_id_ = 0;
};

class stalling_transport_stream final : public forge::net::transport::detail::stream_concept {
 public:
   explicit stalling_transport_stream(std::int64_t stream_id) : stream_id_{stream_id} {}

   [[nodiscard]] bool valid() const noexcept override {
      return true;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return stream_id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t>) override {
      co_return;
   }

   boost::asio::awaitable<void> async_write_chunk(forge::net::transport::chunk) override {
      co_return;
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      auto timer = std::make_shared<boost::asio::steady_timer>(co_await boost::asio::this_coro::executor);
      timer->expires_at(std::chrono::steady_clock::time_point::max());
      {
         auto lock = std::scoped_lock{mutex_};
         timer_ = timer;
      }
      auto error = boost::system::error_code{};
      co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw boost::system::system_error{error};
      }
      co_return std::vector<std::uint8_t>{};
   }

   boost::asio::awaitable<forge::net::transport::chunk> async_read_chunk() override {
      co_return forge::net::transport::chunk{co_await async_read()};
   }

   boost::asio::awaitable<void> async_close() override {
      closed = true;
      co_return;
   }

   void cancel() override {
      closed = true;
      auto lock = std::scoped_lock{mutex_};
      if (timer_) {
         timer_->cancel();
      }
   }

   bool closed = false;

 private:
   std::int64_t stream_id_ = 0;
   std::mutex mutex_;
   std::shared_ptr<boost::asio::steady_timer> timer_;
};

class tracking_peer_store_persistence final : public peer_store::persistence {
 public:
   boost::asio::awaitable<peer_store::hydration_page> async_hydrate(peer_store::hydration_request request) override {
      hydration_requests.push_back(request);
      if (fail_hydrate) {
         throw std::runtime_error{"injected peer hydration failure"};
      }
      if (block_hydrate && request.kind == peer_store::hydration_kind::peers) {
         auto executor = co_await boost::asio::this_coro::executor;
         auto timer = std::shared_ptr<boost::asio::steady_timer>{};
         {
            auto lock = std::scoped_lock{block_mutex};
            if (!hydrate_blocked_once) {
               timer = std::make_shared<boost::asio::steady_timer>(executor);
               timer->expires_at(std::chrono::steady_clock::time_point::max());
               hydrate_timer = timer;
               hydrate_blocked = true;
               hydrate_blocked_once = true;
            }
         }
         if (timer) {
            block_changed.notify_all();
            auto error = boost::system::error_code{};
            co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         }
      }
      co_return co_await delegate->async_hydrate(std::move(request));
   }

   boost::asio::awaitable<peer_store::apply_result> async_apply(peer_store::mutation_batch batch) override {
      ++apply_attempts;
      if (fail_apply) {
         throw std::runtime_error{"injected peer apply failure"};
      }
      if (block_apply) {
         auto timer = std::make_shared<boost::asio::steady_timer>(co_await boost::asio::this_coro::executor);
         timer->expires_at(std::chrono::steady_clock::time_point::max());
         {
            auto lock = std::scoped_lock{block_mutex};
            apply_timer = timer;
            apply_blocked = true;
         }
         block_changed.notify_all();
         auto error = boost::system::error_code{};
         co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
      const auto peer_upserts = batch.peer_upserts.size();
      const auto peer_removals = batch.peer_removals.size();
      const auto rendezvous_upserts = batch.rendezvous_upserts.size();
      const auto rendezvous_removals = batch.rendezvous_removals.size();
      const auto durable = batch.durable;
      auto rendezvous_high_watermark = batch.rendezvous_sequence_high_watermark;
      for (const auto& value : batch.rendezvous_upserts) {
         rendezvous_high_watermark = std::max(rendezvous_high_watermark, value.sequence);
      }
      auto result = co_await delegate->async_apply(std::move(batch));
      applied_peer_upserts += peer_upserts;
      applied_peer_removals += peer_removals;
      applied_rendezvous_upserts += rendezvous_upserts;
      applied_rendezvous_removals += rendezvous_removals;
      applied_rendezvous_high_watermark = std::max(applied_rendezvous_high_watermark, rendezvous_high_watermark);
      durable_apply_attempts += durable ? 1U : 0U;
      if (durable && uncertain_durable_apply) {
         result.durability_confirmed = false;
         result.durability_failure = "injected uncertain durable acknowledgement";
      }
      co_return result;
   }

   boost::asio::awaitable<peer_store::prune_result> async_prune_expired(std::chrono::system_clock::time_point now,
                                                                        std::size_t limit) override {
      prune_limits.push_back(limit);
      if (fail_prune) {
         throw std::runtime_error{"injected peer prune failure"};
      }
      if (block_prune) {
         auto timer = std::make_shared<boost::asio::steady_timer>(co_await boost::asio::this_coro::executor);
         timer->expires_at(std::chrono::steady_clock::time_point::max());
         {
            auto lock = std::scoped_lock{block_mutex};
            prune_timer = timer;
            prune_blocked = true;
         }
         block_changed.notify_all();
         auto error = boost::system::error_code{};
         co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
      co_return co_await delegate->async_prune_expired(now, limit);
   }

   boost::asio::awaitable<void> async_flush() override {
      ++flush_attempts;
      if (fail_flush) {
         throw std::runtime_error{"injected peer flush failure"};
      }
      co_await delegate->async_flush();
   }

   boost::asio::awaitable<void> async_close() override {
      ++close_attempts;
      if (fail_close) {
         throw std::runtime_error{"injected peer close failure"};
      }
      if (!retain_delegate_on_close) {
         co_await delegate->async_close();
      }
   }

   [[nodiscard]] bool wait_until_apply_blocked() {
      auto lock = std::unique_lock{block_mutex};
      return block_changed.wait_for(lock, std::chrono::seconds{2}, [&] { return apply_blocked; });
   }

   [[nodiscard]] bool wait_until_hydrate_blocked() {
      auto lock = std::unique_lock{block_mutex};
      return block_changed.wait_for(lock, std::chrono::seconds{2}, [&] { return hydrate_blocked; });
   }

   [[nodiscard]] bool wait_until_prune_blocked() {
      auto lock = std::unique_lock{block_mutex};
      return block_changed.wait_for(lock, std::chrono::seconds{2}, [&] { return prune_blocked; });
   }

   void release_apply() {
      auto timer = std::shared_ptr<boost::asio::steady_timer>{};
      {
         auto lock = std::scoped_lock{block_mutex};
         timer = apply_timer;
         apply_blocked = false;
         apply_timer.reset();
      }
      if (timer) {
         boost::asio::post(timer->get_executor(), [timer] { timer->cancel(); });
      }
   }

   void release_hydrate() {
      auto timer = std::shared_ptr<boost::asio::steady_timer>{};
      {
         auto lock = std::scoped_lock{block_mutex};
         timer = hydrate_timer;
         hydrate_blocked = false;
         hydrate_timer.reset();
      }
      if (timer) {
         boost::asio::post(timer->get_executor(), [timer] { timer->cancel(); });
      }
   }

   void release_prune() {
      auto timer = std::shared_ptr<boost::asio::steady_timer>{};
      {
         auto lock = std::scoped_lock{block_mutex};
         timer = prune_timer;
         prune_blocked = false;
         prune_timer.reset();
      }
      if (timer) {
         boost::asio::post(timer->get_executor(), [timer] { timer->cancel(); });
      }
   }

   std::shared_ptr<peer_store::persistence> delegate = peer_store::make_memory_persistence();
   std::vector<peer_store::hydration_request> hydration_requests;
   std::vector<std::size_t> prune_limits;
   std::size_t apply_attempts = 0;
   std::size_t applied_peer_upserts = 0;
   std::size_t applied_peer_removals = 0;
   std::size_t applied_rendezvous_upserts = 0;
   std::size_t applied_rendezvous_removals = 0;
   std::uint64_t applied_rendezvous_high_watermark = 0;
   std::size_t durable_apply_attempts = 0;
   std::size_t flush_attempts = 0;
   std::size_t close_attempts = 0;
   bool fail_hydrate = false;
   bool fail_apply = false;
   bool uncertain_durable_apply = false;
   bool fail_prune = false;
   bool fail_flush = false;
   bool fail_close = false;
   bool block_apply = false;
   bool block_hydrate = false;
   bool block_prune = false;
   bool retain_delegate_on_close = false;
   std::mutex block_mutex;
   std::condition_variable block_changed;
   std::shared_ptr<boost::asio::steady_timer> apply_timer;
   std::shared_ptr<boost::asio::steady_timer> hydrate_timer;
   std::shared_ptr<boost::asio::steady_timer> prune_timer;
   bool apply_blocked = false;
   bool hydrate_blocked = false;
   bool hydrate_blocked_once = false;
   bool prune_blocked = false;
};

class tracking_dht_record_store_persistence final : public dht::record_store::persistence {
 public:
   boost::asio::awaitable<dht::record_store::hydration_page>
   async_hydrate(dht::record_store::hydration_request request) override {
      co_return co_await delegate->async_hydrate(std::move(request));
   }

   boost::asio::awaitable<dht::record_store::apply_result>
   async_apply(dht::record_store::mutation_batch batch) override {
      const auto previous_upserts =
          provider_upsert_attempts.fetch_add(batch.provider_upserts.size(), std::memory_order_relaxed);
      provider_remove_attempts.fetch_add(batch.provider_removals.size(), std::memory_order_relaxed);
      if (!batch.provider_removals.empty() && reject_next_provider_removal.exchange(false, std::memory_order_relaxed)) {
         throw std::runtime_error{"injected DHT provider removal failure"};
      }
      if (!batch.provider_upserts.empty() && before_provider_apply) {
         auto callback = std::exchange(before_provider_apply, {});
         callback();
      }
      if (reject_provider_upserts && !batch.provider_upserts.empty()) {
         FORGE_THROW_CODE(exceptions::code::backpressure_rejected, "injected DHT provider persistence backpressure");
      }
      if (reject_provider_upserts_after_first && previous_upserts != 0 && !batch.provider_upserts.empty()) {
         FORGE_THROW_CODE(exceptions::code::backpressure_rejected,
                          "injected DHT provider republish persistence backpressure");
      }
      co_return co_await delegate->async_apply(std::move(batch));
   }

   boost::asio::awaitable<dht::record_store::prune_result>
   async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) override {
      co_return co_await delegate->async_prune_expired(now, limit);
   }

   boost::asio::awaitable<void> async_flush() override {
      co_await delegate->async_flush();
   }

   boost::asio::awaitable<void> async_close() override {
      close_attempts.fetch_add(1, std::memory_order_relaxed);
      co_await delegate->async_close();
   }

   std::shared_ptr<dht::record_store::persistence> delegate = dht::record_store::make_memory_persistence();
   std::atomic_size_t provider_upsert_attempts = 0;
   std::atomic_size_t provider_remove_attempts = 0;
   std::atomic_size_t close_attempts = 0;
   std::atomic_bool reject_next_provider_removal = false;
   std::function<void()> before_provider_apply;
   bool reject_provider_upserts = false;
   bool reject_provider_upserts_after_first = false;
};

void seed_dht_provider_records(forge::asio::runtime& runtime, const dht::profile& profile,
                               const std::shared_ptr<dht::record_store::persistence>& persistence,
                               std::vector<dht::record_store::provider_record> providers) {
   auto store = dht::record_store{profile, dht::record_store::options{.persistence = persistence}};
   for (auto& provider : providers) {
      forge::asio::blocking::run(runtime, store.async_upsert_provider(std::move(provider)));
   }
   forge::asio::blocking::run(runtime, store.async_flush());
}

[[nodiscard]] std::size_t dht_bucket_for(const peer_id& local, const peer_id& candidate) {
   const auto distance = distance_between(local.to_bytes(), candidate.to_bytes());
   auto prefix_bits = std::size_t{};
   for (const auto byte : distance.bytes) {
      if (byte == 0) {
         prefix_bits += 8;
         continue;
      }
      prefix_bits += static_cast<std::size_t>(std::countl_zero(byte));
      break;
   }
   return std::min(prefix_bits, std::size_t{255});
}

[[nodiscard]] std::vector<peer_id> peers_in_same_dht_bucket(const peer_id& local, std::size_t count) {
   auto buckets = std::map<std::size_t, std::vector<peer_id>>{};
   for (auto value = std::uint16_t{}; value <= 255; ++value) {
      auto candidate = peer(static_cast<std::uint8_t>(value));
      if (candidate == local) {
         continue;
      }
      auto& bucket = buckets[dht_bucket_for(local, candidate)];
      bucket.push_back(std::move(candidate));
      if (bucket.size() == count) {
         return bucket;
      }
   }
   throw std::runtime_error{"not enough deterministic peers in one DHT bucket"};
}

} // namespace

BOOST_AUTO_TEST_CASE(p2p_identity_uses_libp2p_multihash_shape) {
   const auto identity = make_test_certificate_identity("p2p-identity-shape");
   const auto id = make_peer_id_from_certificate_pem(identity.certificate_pem);

   BOOST_TEST(id.to_string() == identity.peer.to_string());
   BOOST_TEST(valid_peer_id(id));
   auto decoded = forge::multiformats::multihash::decode(id.to_bytes());
   BOOST_TEST(decoded.code == forge::multiformats::code_value(forge::multiformats::multicodec_code::sha2_256));
   BOOST_TEST(decoded.digest.size() == 32U);
}

BOOST_AUTO_TEST_CASE(p2p_certificate_without_libp2p_extension_is_rejected) {
   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_pem(test_certificate()), exceptions::invalid_identity);
   const auto certificate = forge::crypto::pki::x509::certificate::from_pem(test_certificate());
   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_der(certificate.der()), exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_public_key_encoding_matches_libp2p_vectors) {
   const auto ed25519_public_key = bytes_from_hex("1ed1e8fae2c4a144b8be8fd4b47bf3d3b34b871c3cacf6010f0e42d474fce27e");
   const auto ed25519_encoded = encode_public_key({.type = public_key::type::ed25519, .data = ed25519_public_key});
   BOOST_CHECK_EQUAL(forge::multiformats::multihash::identity(ed25519_encoded).digest_hex(),
                     "080112201ed1e8fae2c4a144b8be8fd4b47bf3d3b34b871c3cacf6010f0e42d474fce27e");

   auto ed25519_peer = make_peer_id({.type = public_key::type::ed25519, .data = ed25519_public_key});
   auto ed25519_hash = forge::multiformats::multihash::decode(ed25519_peer.to_bytes());
   BOOST_TEST(ed25519_hash.code == forge::multiformats::code_value(forge::multiformats::multicodec_code::identity));

   const auto ecdsa_public_key =
       bytes_from_hex("3059301306072a8648ce3d020106082a8648ce3d03010703420004de3d300fa36ae0e8f5d530899d83abab44ab"
                      "f3161f162a4bc901d8e6ecda020e8b6d5f8da30525e71d6851510c098e5c47c646a597fb4dcec034e9f77c409e62");
   auto ecdsa_peer = make_peer_id({.type = public_key::type::ecdsa, .data = ecdsa_public_key});
   auto ecdsa_hash = forge::multiformats::multihash::decode(ecdsa_peer.to_bytes());
   BOOST_TEST(ecdsa_hash.code == forge::multiformats::code_value(forge::multiformats::multicodec_code::sha2_256));
}

BOOST_AUTO_TEST_CASE(p2p_certificate_extension_rejects_unverified_non_ed25519_identity) {
   const auto identity = make_secp256k1_identity();
   auto bogus_signature = std::vector<std::uint8_t>(72, 0x42);
   const auto extension = signed_key_der(encode_public_key(identity.key), bogus_signature);
   const auto certificate = make_certificate_der_with_libp2p_extension(extension);

   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_der(certificate), exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_certificate_extension_rejects_overflowing_identity_octet_length) {
   const auto extension = signed_key_der_with_overflowing_octet_length();
   const auto certificate = make_certificate_der_with_libp2p_extension(extension);

   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_der(certificate), exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_certificate_extension_verifies_supported_identities) {
   const auto identities = std::vector<test_identity>{make_test_identity(), make_rsa_identity(),
                                                      make_secp256k1_identity(), make_p256_identity()};
   for (const auto& identity : identities) {
      const auto certificate = make_certificate_der_with_libp2p_extension_from_factory(
          [&](std::span<const std::uint8_t> certificate_public_key) {
             return signed_tls_extension(identity, certificate_public_key);
          });

      BOOST_TEST(make_peer_id_from_certificate_der(certificate).to_string() == identity.peer.to_string());
   }
}

BOOST_AUTO_TEST_CASE(p2p_identity_signatures_support_supported_key_types) {
   const auto identities = std::vector<test_identity>{make_test_identity(), make_rsa_identity(),
                                                      make_secp256k1_identity(), make_p256_identity()};
   for (const auto& identity : identities) {
      auto message = pubsub::message{
          .from = identity.peer,
          .data = std::vector<std::uint8_t>{'m', 'u', 'l', 't', 'i', '-', 'k', 'e', 'y'},
          .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 9},
          .subject = pubsub::topic{.value = "forge.identity"},
          .key = encode_public_key(identity.key),
      };
      pubsub::codec::sign_message(message, identity.private_key);
      BOOST_TEST(pubsub::codec::verify_message(message));

      auto tampered = message;
      tampered.signature.back() ^= 0x01U;
      BOOST_TEST(!pubsub::codec::verify_message(tampered));

      const auto payload_type = forge::multiformats::varint_encode(0x0302);
      const auto payload = std::vector<std::uint8_t>{7, 8, 9};
      const auto envelope =
          signed_envelope::seal(identity.key, identity.private_key, "libp2p-relay-rsvp", payload_type, payload);
      BOOST_CHECK_NO_THROW(envelope.verify("libp2p-relay-rsvp", identity.peer));

      auto tampered_envelope = envelope;
      tampered_envelope.payload.back() ^= 0x01U;
      BOOST_CHECK_THROW(tampered_envelope.verify("libp2p-relay-rsvp", identity.peer), exceptions::invalid_identity);

      auto malformed_envelope = envelope;
      malformed_envelope.signature.pop_back();
      BOOST_CHECK_THROW(malformed_envelope.verify("libp2p-relay-rsvp", identity.peer), exceptions::invalid_identity);
   }
}

BOOST_AUTO_TEST_CASE(p2p_peer_id_legacy_and_cid_strings_roundtrip) {
   auto id =
       make_peer_id({.type = public_key::type::secp256k1,
                     .data = bytes_from_hex("037777e994e452c21604f91de093ce415f5432f701dd8cd1a7a6fea0e630bfca99")});

   auto legacy = id.to_string();
   BOOST_TEST(peer_id::from_string(legacy).to_string() == id.to_string());

   auto cid = id.to_cid_string();
   BOOST_TEST(cid.front() == 'b');
   BOOST_TEST(peer_id::from_string(cid).to_string() == id.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_endpoint_parses_libp2p_quic_address_format) {
   static_assert(std::is_same_v<decltype(endpoint{}.transport.host_type), endpoint::host_kind>);

   const auto id = peer(42);
   auto parsed = parse_endpoint("/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string());

   BOOST_TEST(static_cast<int>(parsed.transport.host_type) == static_cast<int>(endpoint::host_kind::ip4));

   BOOST_TEST(parsed.transport.host == "127.0.0.1");
   BOOST_TEST(parsed.transport.port == 4001);
   BOOST_REQUIRE(parsed.peer.has_value());
   BOOST_TEST(parsed.peer->to_string() == id.to_string());
   BOOST_TEST(parsed.to_string() == "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string());
   BOOST_TEST(parsed.is_direct_quic());
}

BOOST_AUTO_TEST_CASE(p2p_endpoint_uses_multiaddr_for_tcp_wss_and_relay_views) {
   const auto id = peer(43);

   auto tcp = parse_endpoint("/dns4/example.com/tcp/4001/p2p/" + id.to_string());
   BOOST_TEST(static_cast<int>(tcp.transport.host_type) == static_cast<int>(endpoint::host_kind::dns4));
   BOOST_TEST(static_cast<int>(tcp.transport.protocol) == static_cast<int>(endpoint::protocol_kind::tcp));
   BOOST_TEST(tcp.transport.host == "example.com");
   BOOST_TEST(tcp.transport.port == 4001);
   BOOST_TEST(tcp.is_direct_tcp());
   BOOST_TEST(tcp.to_string() == "/dns4/example.com/tcp/4001/p2p/" + id.to_string());

   auto wss = parse_endpoint("/dns4/example.com/tcp/443/wss/p2p/" + id.to_string());
   BOOST_TEST(!wss.is_direct_tcp());
   BOOST_TEST(!wss.is_direct_quic());
   BOOST_TEST(wss.to_string() == "/dns4/example.com/tcp/443/wss/p2p/" + id.to_string());

   auto relayed = parse_endpoint("/ip4/127.0.0.1/tcp/9090/p2p-circuit/p2p/" + id.to_string());
   BOOST_TEST(!relayed.peer.has_value());
   BOOST_REQUIRE(relayed.relayed.has_value());
   BOOST_TEST(relayed.relayed->target.to_string() == id.to_string());
   BOOST_TEST(relayed.to_string() == "/ip4/127.0.0.1/tcp/9090/p2p-circuit/p2p/" + id.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_websocket_multiaddr_is_parseable_but_not_dialable) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(44))};
   const auto endpoints = std::vector<endpoint>{
       parse_endpoint("/dns4/example.com/tcp/80/ws/p2p/" + peer(46).to_string()),
       parse_endpoint("/dns4/example.com/tcp/443/wss/p2p/" + peer(47).to_string()),
   };

   for (const auto& endpoint : endpoints) {
      try {
         forge::asio::blocking::run(runtime, value.async_listen(endpoint));
         BOOST_FAIL("expected unsupported listen endpoint");
      } catch (const forge::exceptions::base& error) {
         BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
         BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                    static_cast<int>(exceptions::code::unsupported_protocol));
      }

      try {
         forge::asio::blocking::run(runtime, value.async_connect(endpoint));
         BOOST_FAIL("expected unsupported connect endpoint");
      } catch (const forge::exceptions::base& error) {
         BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
         BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                    static_cast<int>(exceptions::code::unsupported_protocol));
      }
   }
}

BOOST_AUTO_TEST_CASE(p2p_node_listens_on_quic_and_tcp_and_identify_advertises_both) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(210))};
   auto client = node{runtime, options_for(peer(211))};

   forge::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   forge::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));

   const auto local = server.local_endpoints();
   BOOST_REQUIRE_EQUAL(local.size(), 2U);
   BOOST_TEST(contains_protocol(local, endpoint::protocol_kind::quic_v1));
   BOOST_TEST(contains_protocol(local, endpoint::protocol_kind::tcp));
   for (const auto& item : local) {
      BOOST_REQUIRE(item.peer.has_value());
      BOOST_TEST(item.peer->value == server.local_peer().value);
   }
   BOOST_REQUIRE(server.local_endpoint().has_value());

   const auto quic = require_endpoint_for(local, endpoint::protocol_kind::quic_v1);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(quic, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto payload = forge::asio::blocking::run(runtime, read_length_delimited(stream));
   const auto doc = identify::decode(payload);
   BOOST_TEST(contains_protocol(doc.listen_endpoints, endpoint::protocol_kind::quic_v1));
   BOOST_TEST(contains_protocol(doc.listen_endpoints, endpoint::protocol_kind::tcp));
   for (const auto& item : doc.listen_endpoints) {
      BOOST_REQUIRE(item.peer.has_value());
      BOOST_TEST(item.peer->value == server.local_peer().value);
   }

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_response_contains_canonical_signed_record_and_observed_address) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-standard-record-server");
   const auto client_identity = make_test_certificate_identity("identify-standard-record-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto document = identify::decode(forge::asio::blocking::run(runtime, read_length_delimited(stream)));

   BOOST_REQUIRE(document.observed_endpoint.has_value());
   BOOST_TEST(document.observed_endpoint->transport.port != 0U);
   BOOST_REQUIRE(!document.signed_peer_record.empty());
   const auto envelope = signed_envelope::decode(document.signed_peer_record);
   BOOST_TEST(envelope.payload_type == identify_peer_record_payload_type(), boost::test_tools::per_element());
   BOOST_CHECK_NO_THROW(envelope.verify("libp2p-peer-record", server.local_peer()));
   const auto record = rendezvous::codec::decode_peer_record(envelope.payload);
   BOOST_TEST(record.peer.to_string() == server.local_peer().to_string());
   BOOST_TEST(record.sequence > 0U);
   BOOST_REQUIRE(!record.endpoints.empty());

   auto second_stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto second_document =
       identify::decode(forge::asio::blocking::run(runtime, read_length_delimited(second_stream)));
   BOOST_TEST(second_document.signed_peer_record == document.signed_peer_record, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_completes_for_multiple_inbound_sessions) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto server_identity = make_test_certificate_identity("identify-multiple-server");
   const auto first_identity = make_test_certificate_identity("identify-multiple-first");
   const auto second_identity = make_test_certificate_identity("identify-multiple-second");
   auto server = node{runtime, options_for(server_identity)};
   auto first = node{runtime, options_for(first_identity)};
   auto second = node{runtime, options_for(second_identity)};
   const auto server_endpoint = listen(server, runtime);

   const auto connect = [&](node& client) {
      return forge::asio::blocking::run(runtime,
                                        client.async_connect(server_endpoint, node::connect_options{
                                                                                  .expected_peer = server.local_peer(),
                                                                                  .timeout = std::chrono::seconds{2},
                                                                              }));
   };
   const auto first_session = connect(first);
   const auto second_session = connect(second);

   BOOST_TEST(static_cast<int>(first_session.identify_state) == static_cast<int>(identify::state::identified));
   BOOST_TEST(static_cast<int>(second_session.identify_state) == static_cast<int>(identify::state::identified));

   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_response_is_trimmed_to_rust_message_limit) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-trim-server");
   const auto client_identity = make_test_certificate_identity("identify-trim-client");
   auto advertised = std::vector<endpoint>{};
   for (auto port = std::uint16_t{4300}; port < 4364; ++port) {
      advertised.push_back(parse_endpoint("/ip4/127.0.0.1/udp/" + std::to_string(port) + "/quic-v1/p2p/" +
                                          server_identity.peer.to_string()));
   }
   const auto advertised_count = advertised.size();
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   server.set_advertised_endpoints(std::move(advertised));
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto payload = forge::asio::blocking::run(runtime, read_length_delimited(stream));
   const auto document = identify::decode(payload);

   BOOST_TEST(payload.size() <= identify::limits{}.max_own_message_size);
   BOOST_TEST(document.listen_endpoints.size() < advertised_count);
   BOOST_REQUIRE(!document.signed_peer_record.empty());
   BOOST_TEST(rendezvous::codec::decode_peer_record(signed_envelope::decode(document.signed_peer_record).payload)
                  .endpoints.size() == document.listen_endpoints.size());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_peer_record_sequence_advances_across_node_restart) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto restarted_identity = make_test_certificate_identity("identify-restart-peer");
   const auto observer_identity = make_test_certificate_identity("identify-restart-observer");
   auto observer = std::make_unique<node>(runtime, options_for(observer_identity));

   auto first = std::make_unique<node>(runtime, options_for(restarted_identity));
   const auto first_endpoint = listen(*first, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, observer->async_connect(first_endpoint, node::connect_options{.expected_peer = first->local_peer()})));
   const auto first_record = observer->peers().find(first->local_peer());
   BOOST_REQUIRE(first_record);
   const auto first_sequence = identify_peer_record_sequence(first_record->signed_peer_record);

   forge::asio::blocking::run(runtime, observer->async_stop());
   observer.reset();
   forge::asio::blocking::run(runtime, first->async_stop());
   first.reset();

   observer = std::make_unique<node>(runtime, options_for(observer_identity));
   observer->peers().upsert(*first_record);
   auto second = std::make_unique<node>(runtime, options_for(restarted_identity));
   const auto second_endpoint = listen(*second, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime,
       observer->async_connect(second_endpoint, node::connect_options{.expected_peer = second->local_peer()})));
   const auto second_record = observer->peers().find(second->local_peer());
   BOOST_REQUIRE(second_record);
   BOOST_TEST(identify_peer_record_sequence(second_record->signed_peer_record) > first_sequence);

   forge::asio::blocking::run(runtime, observer->async_stop());
   forge::asio::blocking::run(runtime, second->async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_idle_listeners_do_not_consume_pending_inbound_session_budget) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(peer(198));
   server_options.limits.max_pending_inbound_sessions = 1;
   auto server = node{runtime, std::move(server_options)};

   forge::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   forge::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   wait_on_runtime(runtime, std::chrono::milliseconds{20}, "idle P2P listeners");

   BOOST_TEST(server.diagnostics().resources.pending_inbound_sessions == 0U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_tcp_handshake_is_admitted_before_security_upgrade) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = options_for(peer(199));
   server_options.limits.max_pending_inbound_sessions = 1;
   auto server = node{runtime, std::move(server_options)};

   forge::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   forge::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   auto listeners = server.local_endpoints();
   std::erase_if(listeners, [](const endpoint& value) { return !value.is_direct_tcp(); });
   BOOST_REQUIRE_EQUAL(listeners.size(), 2U);

   auto first = boost::asio::ip::tcp::socket{runtime.context()};
   auto second = boost::asio::ip::tcp::socket{runtime.context()};
   const auto connect_raw = [](boost::asio::ip::tcp::socket& socket,
                               const endpoint& target) -> boost::asio::awaitable<void> {
      co_await socket.async_connect(
          boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address(target.transport.host), target.transport.port},
          boost::asio::use_awaitable);
   };

   forge::asio::blocking::run(runtime, connect_raw(first, listeners[0]));
   const auto admitted_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (server.diagnostics().resources.pending_inbound_sessions != 1U) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < admitted_deadline);
      wait_on_runtime(runtime, std::chrono::milliseconds{1}, "first raw TCP admission");
   }

   forge::asio::blocking::run(runtime, connect_raw(second, listeners[1]));
   const auto rejected_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (server.diagnostics().resources.denied_sessions == 0U) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < rejected_deadline);
      wait_on_runtime(runtime, std::chrono::milliseconds{1}, "second raw TCP admission rejection");
   }
   const auto bounded = server.diagnostics().resources;
   BOOST_TEST(bounded.pending_inbound_sessions == 1U);
   BOOST_TEST(bounded.denied_sessions >= 1U);

   auto ignored = boost::system::error_code{};
   first.close(ignored);
   second.close(ignored);
   forge::asio::blocking::run(runtime, server.async_stop());
   BOOST_TEST(server.diagnostics().resources.pending_inbound_sessions == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_duplicate_direct_listen_rejects_typed) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(212))};

   forge::asio::blocking::run(runtime, value.async_listen(make_tcp_endpoint(0)));
   const auto local = require_endpoint_for(value.local_endpoints(), endpoint::protocol_kind::tcp);

   try {
      forge::asio::blocking::run(runtime, value.async_listen(local));
      BOOST_FAIL("expected duplicate listen rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }

   forge::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_preserves_multiple_direct_endpoints_without_duplicates) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("peer-exchange-multi-server");
   const auto client_identity = make_test_certificate_identity("peer-exchange-multi-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   forge::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   forge::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   const auto advertised = server.local_endpoints();
   BOOST_REQUIRE_EQUAL(advertised.size(), 2U);

   const auto quic = require_endpoint_for(advertised, endpoint::protocol_kind::quic_v1);
   client.peers().learn_endpoint(server.local_peer(), quic,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange});
   forge::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));
   forge::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));

   const auto learned = client.peers().find(server.local_peer());
   BOOST_REQUIRE(learned);
   auto learned_endpoints = std::vector<endpoint>{};
   learned_endpoints.reserve(learned->endpoints.size());
   for (const auto& item : learned->endpoints) {
      learned_endpoints.push_back(item.endpoint);
   }
   BOOST_TEST(contains_protocol(learned_endpoints, endpoint::protocol_kind::quic_v1));
   BOOST_TEST(contains_protocol(learned_endpoints, endpoint::protocol_kind::tcp));
   auto seen = std::set<std::string>{};
   for (const auto& item : learned->endpoints) {
      BOOST_REQUIRE(item.endpoint.peer.has_value());
      BOOST_TEST(item.endpoint.peer->to_bytes() == server.local_peer().to_bytes(), boost::test_tools::per_element());
      BOOST_TEST(seen.insert(item.endpoint.to_string()).second);
   }

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_rejects_spoofed_response_identity_without_mutating_victim) {
   auto store = peer_store{};
   const auto authenticated_peer = peer(218);
   const auto victim = peer(219);
   auto original_endpoint = make_tcp_endpoint(4001, "8.8.8.8");
   original_endpoint.peer = victim;
   store.upsert(peer_store::record{
       .peer = victim,
       .capabilities = capability_set{.bits = capabilities::peer_exchange},
   });
   store.learn_endpoint(victim, original_endpoint, capability_set{.bits = capabilities::peer_exchange});
   const auto before = store.find(victim);
   BOOST_REQUIRE(before);

   auto spoofed_endpoint = make_tcp_endpoint(4002, "1.1.1.1");
   spoofed_endpoint.peer = victim;
   const auto response = peer_exchange_message{
       .kind = peer_exchange_message::type::peer_exchange_response,
       .peer = victim,
       .capabilities = capability_set{.bits = capabilities::pubsub},
       .endpoints = {{
           .peer = victim,
           .endpoint = spoofed_endpoint,
           .capabilities = capability_set{.bits = capabilities::pubsub},
       }},
   };
   try {
      detail::learn_authenticated_peer_exchange_response(store, response, authenticated_peer);
      BOOST_FAIL("expected spoofed peer exchange identity rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   const auto after = store.find(victim);
   BOOST_REQUIRE(after);
   BOOST_TEST(after->capabilities.bits == before->capabilities.bits);
   BOOST_REQUIRE_EQUAL(after->endpoints.size(), before->endpoints.size());
   BOOST_TEST(after->endpoints.front().endpoint.to_string() == before->endpoints.front().endpoint.to_string());
   BOOST_TEST(!store.find(authenticated_peer).has_value());
}

BOOST_AUTO_TEST_CASE(p2p_local_endpoints_collapse_canonical_equivalent_advertised_endpoints) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = peer(219);
   auto configured = make_tcp_endpoint(4001, "127.0.0.1");
   configured.peer = local;
   auto equivalent = parse_endpoint(configured.to_string());
   equivalent.peer = std::nullopt;
   auto options = options_for(local);
   options.advertised_endpoints = {configured, equivalent};
   auto value = node{runtime, std::move(options)};

   const auto endpoints = value.local_endpoints();
   BOOST_REQUIRE_EQUAL(endpoints.size(), 1U);
   BOOST_REQUIRE(endpoints.front().peer.has_value());
   BOOST_TEST(endpoints.front().peer->to_bytes() == local.to_bytes(), boost::test_tools::per_element());
   BOOST_TEST(endpoints.front().to_string() == configured.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_filters_non_routable_third_party_endpoints) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("peer-exchange-filter-server");
   const auto client_identity = make_test_certificate_identity("peer-exchange-filter-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};
   const auto third = peer(220);

   auto loopback = make_tcp_endpoint(4001, "127.0.0.1");
   auto private_endpoint = make_tcp_endpoint(4002, "192.168.1.10");
   auto link_local = make_tcp_endpoint(4003, "169.254.1.10");
   auto public_endpoint = make_tcp_endpoint(4004, "8.8.4.4");
   auto dns_endpoint = make_dns_tcp_endpoint(4005, "example.com");
   auto localhost_dns = make_dns_tcp_endpoint(4006, "localhost");
   for (auto* endpoint_value :
        {&loopback, &private_endpoint, &link_local, &public_endpoint, &dns_endpoint, &localhost_dns}) {
      endpoint_value->peer = third;
      server.peers().learn_endpoint(third, *endpoint_value, capability_set{});
   }

   forge::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   const auto quic = require_endpoint_for(server.local_endpoints(), endpoint::protocol_kind::quic_v1);
   client.peers().learn_endpoint(server.local_peer(), quic,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange});
   forge::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));

   const auto learned = client.peers().find(third);
   BOOST_REQUIRE(learned);
   auto seen = std::set<std::string>{};
   for (const auto& item : learned->endpoints) {
      seen.insert(item.endpoint.to_string());
   }
   BOOST_TEST(seen.contains(public_endpoint.to_string()));
   BOOST_TEST(seen.contains(dns_endpoint.to_string()));
   BOOST_TEST(!seen.contains(loopback.to_string()));
   BOOST_TEST(!seen.contains(private_endpoint.to_string()));
   BOOST_TEST(!seen.contains(link_local.to_string()));
   BOOST_TEST(!seen.contains(localhost_dns.to_string()));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_host_addresses_rejects_third_party_relay_with_non_routable_transport) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("peer-exchange-relay-filter-server");
   const auto client_identity = make_test_certificate_identity("peer-exchange-relay-filter-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};
   const auto third = peer(221);

   const auto loopback_relay = parse_endpoint("/ip4/127.0.0.1/tcp/9090/p2p-circuit/p2p/" + third.to_string());
   const auto private_relay = parse_endpoint("/ip4/10.0.0.7/tcp/9090/p2p-circuit/p2p/" + third.to_string());
   const auto link_local_relay = parse_endpoint("/ip4/169.254.10.7/tcp/9090/p2p-circuit/p2p/" + third.to_string());
   const auto public_relay = parse_endpoint("/ip4/8.8.8.8/tcp/9090/p2p-circuit/p2p/" + third.to_string());
   const auto dns_relay = parse_endpoint("/dns4/relay.example.com/tcp/9090/p2p-circuit/p2p/" + third.to_string());
   for (const auto& endpoint_value : {loopback_relay, private_relay, link_local_relay, public_relay, dns_relay}) {
      server.peers().learn_endpoint(third, endpoint_value, capability_set{});
   }

   forge::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   const auto quic = require_endpoint_for(server.local_endpoints(), endpoint::protocol_kind::quic_v1);
   client.peers().learn_endpoint(server.local_peer(), quic,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange});
   forge::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));

   const auto learned = client.peers().find(third);
   BOOST_REQUIRE(learned);
   auto seen = std::set<std::string>{};
   for (const auto& item : learned->endpoints) {
      seen.insert(item.endpoint.to_string());
   }
   BOOST_TEST(seen.contains(public_relay.to_string()));
   BOOST_TEST(seen.contains(dns_relay.to_string()));
   BOOST_TEST(!seen.contains(loopback_relay.to_string()));
   BOOST_TEST(!seen.contains(private_relay.to_string()));
   BOOST_TEST(!seen.contains(link_local_relay.to_string()));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_discovery_rejects_third_party_non_routable_endpoints) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_profile = amino_v1(dht::mode::server);
   const auto provider_persistence = dht::record_store::make_memory_persistence();
   const auto key =
       amino_provider_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 't', 'r', 'u', 's', 't'});
   const auto now = std::chrono::system_clock::now();
   const auto provider_expiry = now + std::chrono::hours{1};
   const auto address_expiry = now + std::chrono::minutes{30};
   const auto public_provider = peer(223);
   auto public_endpoint = make_dns_tcp_endpoint(4223, "provider.example.com");
   public_endpoint.peer = public_provider;
   const auto loopback_provider = peer(224);
   auto loopback_endpoint = make_tcp_endpoint(4224, "127.0.0.1");
   loopback_endpoint.peer = loopback_provider;
   const auto private_provider = peer(225);
   auto private_endpoint = make_tcp_endpoint(4225, "192.168.50.20");
   private_endpoint.peer = private_provider;
   const auto loopback_relay_provider = peer(228);
   const auto loopback_relay_endpoint =
       parse_endpoint("/ip4/127.0.0.1/tcp/4226/p2p-circuit/p2p/" + loopback_relay_provider.to_string());
   seed_dht_provider_records(runtime, server_profile, provider_persistence,
                             {
                                 {.key = key,
                                  .provider = public_provider,
                                  .endpoints = {public_endpoint},
                                  .provider_expires_at = provider_expiry,
                                  .addresses_expires_at = address_expiry},
                                 {.key = key,
                                  .provider = loopback_provider,
                                  .endpoints = {loopback_endpoint},
                                  .provider_expires_at = provider_expiry,
                                  .addresses_expires_at = address_expiry},
                                 {.key = key,
                                  .provider = private_provider,
                                  .endpoints = {private_endpoint},
                                  .provider_expires_at = provider_expiry,
                                  .addresses_expires_at = address_expiry},
                                 {.key = key,
                                  .provider = loopback_relay_provider,
                                  .endpoints = {loopback_relay_endpoint},
                                  .provider_expires_at = provider_expiry,
                                  .addresses_expires_at = address_expiry},
                             });
   const auto server_identity = make_test_certificate_identity("dht-discovery-server");
   const auto client_identity = make_test_certificate_identity("dht-discovery-client");
   auto server_options = dht_options_for(server_identity, server_profile);
   server_options.dht_record_persistence.emplace(server_profile.protocol, provider_persistence);
   auto client_options = dht_options_for(client_identity, amino_v1());
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   forge::asio::blocking::run(runtime, server.async_hydrate_peer_state());
   const auto server_endpoint = listen(server, runtime);
   verify_dht_server(runtime, client, server, server_endpoint, builtins::kad_dht);

   const auto providers = forge::asio::blocking::run(runtime, client.async_find_providers(builtins::kad_dht, key));
   BOOST_REQUIRE_EQUAL(providers.size(), 4U);
   const auto public_result = std::ranges::find(providers, public_provider, &dht::peer::id);
   BOOST_REQUIRE(public_result != providers.end());
   BOOST_REQUIRE_EQUAL(public_result->endpoints.size(), 1U);
   BOOST_TEST(public_result->endpoints.front().to_string() == public_endpoint.to_string());
   for (const auto& addressless : {loopback_provider, private_provider, loopback_relay_provider}) {
      const auto found = std::ranges::find(providers, addressless, &dht::peer::id);
      BOOST_REQUIRE(found != providers.end());
      BOOST_TEST(found->endpoints.empty());
   }

   const auto queries_before_cached_lookup = server.metrics().dht_queries;
   const auto query_options = dht::query_options{.requested_count = 1};
   const auto cached =
       forge::asio::blocking::run(runtime, client.async_find_providers(builtins::kad_dht, key, query_options));
   BOOST_REQUIRE_EQUAL(cached.size(), 1U);
   BOOST_TEST(cached.front().id.to_string() == public_provider.to_string());
   BOOST_TEST(server.metrics().dht_queries > queries_before_cached_lookup);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_add_provider_preserves_identity_and_continues_rpc_stream) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_profile = amino_v1(dht::mode::server);
   auto server_options = dht_options_for(peer(226), server_profile);
   auto server_store = std::make_shared<tracking_dht_record_store_persistence>();
   server_options.dht_record_persistence.emplace(server_profile.protocol, server_store);
   auto client_options = dht_options_for(peer(227), amino_v1());
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   const auto key = amino_provider_key(
       std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'a', 'd', 'd', '-', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r'});
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::kad_dht));
   auto authenticated_peer = std::optional<peer_id>{};
   for (auto attempt = 0U; attempt < 20U && !authenticated_peer; ++attempt) {
      const auto sessions = server.diagnostics().sessions;
      if (!sessions.empty()) {
         authenticated_peer = sessions.front().remote_peer;
         break;
      }
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "DHT session diagnostics");
   }
   BOOST_REQUIRE(authenticated_peer.has_value());
   auto provider_endpoint = make_dns_tcp_endpoint(4227, "provider-announce.example.com");
   provider_endpoint.peer = *authenticated_peer;
   auto payload = dht::codec::encode(
       dht::message{
           .type = dht::message_type::add_provider,
           .key_value = key,
           .provider_peers = std::vector<dht::peer>{dht::peer{
               .id = *authenticated_peer,
               .endpoints = std::vector<endpoint>{provider_endpoint},
               .connection = dht::connection_type::connected,
           }},
       },
       server_profile);
   const auto ping = dht::codec::encode(dht::message{.type = dht::message_type::ping}, server_profile);
   payload.insert(payload.end(), ping.begin(), ping.end());
   const auto responses_before = server.metrics().dht_responses;
   const auto rejections_before = server.metrics().protocol_rejections;
   forge::asio::blocking::run(runtime, stream.async_write(payload));
   const auto response = dht::codec::decode(
       wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))), server_profile);
   BOOST_TEST(static_cast<int>(response.type) == static_cast<int>(dht::message_type::ping));
   forge::asio::blocking::run(runtime, stream.async_close());
   for (auto attempt = 0U; attempt < 40U && (server_store->provider_upsert_attempts.load() == 0U ||
                                             server.diagnostics().resources.active_streams != 0U);
        ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "DHT ADD_PROVIDER handling");
   }

   BOOST_TEST(server.metrics().dht_queries >= 2U);
   BOOST_TEST(server.metrics().dht_responses == responses_before + 1U);
   BOOST_TEST(server.metrics().protocol_rejections == rejections_before);
   BOOST_TEST(server.diagnostics().resources.active_streams == 0U);
   BOOST_TEST(server_store->provider_upsert_attempts.load(std::memory_order_relaxed) == 1U);
   const auto page =
       forge::asio::blocking::run(runtime, server_store->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::providers,
                                               .limit = 1,
                                           }));
   BOOST_REQUIRE_EQUAL(page.providers.size(), 1U);
   BOOST_TEST(page.providers.front().provider.to_string() == authenticated_peer->to_string());
   BOOST_REQUIRE_EQUAL(page.providers.front().endpoints.size(), 1U);
   BOOST_TEST(page.providers.front().endpoints.front().to_string() == provider_endpoint.to_string());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_add_provider_retains_identity_when_address_is_filtered) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_profile = amino_v1(dht::mode::server);
   auto server_options = dht_options_for(peer(236), server_profile);
   auto server_store = std::make_shared<tracking_dht_record_store_persistence>();
   server_options.dht_record_persistence.emplace(server_profile.protocol, server_store);
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, dht_options_for(peer(237), amino_v1())};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::kad_dht));
   auto authenticated_peer = std::optional<peer_id>{};
   for (auto attempt = 0U; attempt < 20U && !authenticated_peer; ++attempt) {
      const auto sessions = server.diagnostics().sessions;
      if (!sessions.empty()) {
         authenticated_peer = sessions.front().remote_peer;
         break;
      }
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "DHT session diagnostics");
   }
   BOOST_REQUIRE(authenticated_peer.has_value());
   auto provider_endpoint = make_quic_endpoint(4237, "169.254.10.20");
   provider_endpoint.peer = *authenticated_peer;
   const auto key =
       amino_provider_key(std::vector<std::uint8_t>{'a', 'd', 'd', 'r', 'e', 's', 's', 'l', 'e', 's', 's'});
   auto payload = dht::codec::encode(
       dht::message{
           .type = dht::message_type::add_provider,
           .key_value = key,
           .provider_peers = {dht::peer{.id = *authenticated_peer, .endpoints = {provider_endpoint}}},
       },
       server_profile);
   const auto ping = dht::codec::encode(dht::message{.type = dht::message_type::ping}, server_profile);
   payload.insert(payload.end(), ping.begin(), ping.end());
   forge::asio::blocking::run(runtime, stream.async_write(payload));
   const auto response = dht::codec::decode(
       wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))), server_profile);
   BOOST_TEST(static_cast<int>(response.type) == static_cast<int>(dht::message_type::ping));
   forge::asio::blocking::run(runtime, stream.async_close());
   for (auto attempt = 0U; attempt < 40U && server.diagnostics().resources.active_streams != 0U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{25}, "DHT addressless provider stream close");
   }
   BOOST_TEST(server.diagnostics().resources.active_streams == 0U);
   BOOST_TEST(server_store->provider_upsert_attempts.load(std::memory_order_relaxed) == 1U);
   const auto page =
       forge::asio::blocking::run(runtime, server_store->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::providers,
                                               .limit = 1,
                                           }));
   BOOST_REQUIRE_EQUAL(page.providers.size(), 1U);
   BOOST_TEST(page.providers.front().provider.to_string() == authenticated_peer->to_string());
   BOOST_TEST(page.providers.front().endpoints.empty());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_persistent_stream_resets_idle_deadline_after_each_rpc) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto limits = dht::options{};
   limits.query_timeout = std::chrono::milliseconds{250};
   auto server = node{runtime, dht_options_for(peer(238), custom_test_dht_profile(dht::mode::server, limits))};
   auto client = node{runtime, dht_options_for(peer(239), custom_test_dht_profile(dht::mode::client, limits))};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), content_swarm_test_dht));
   const auto ping = dht::codec::encode(dht::message{.type = dht::message_type::ping},
                                        custom_test_dht_profile(dht::mode::client, limits));

   wait_on_runtime(runtime, std::chrono::milliseconds{150}, "first DHT stream idle interval");
   forge::asio::blocking::run(runtime, stream.async_write(ping));
   auto response = dht::codec::decode(
       wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))), limits);
   BOOST_TEST(static_cast<int>(response.type) == static_cast<int>(dht::message_type::ping));

   wait_on_runtime(runtime, std::chrono::milliseconds{150}, "second DHT stream idle interval");
   forge::asio::blocking::run(runtime, stream.async_write(ping));
   response = dht::codec::decode(
       wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))), limits);
   BOOST_TEST(static_cast<int>(response.type) == static_cast<int>(dht::message_type::ping));

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_rejected_value_request_does_not_admit_server_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = dht_options_for(peer(229), amino_v1(dht::mode::server));
   auto client_options = options_for(peer(230), capability_set{.bits = capabilities::direct_quic});
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);

   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::kad_dht));
   forge::asio::blocking::run(runtime, stream.async_write(dht::codec::encode(dht::message{
                                           .type = dht::message_type::get_value,
                                           .key_value = make_dht_key(std::vector<std::uint8_t>{'n', 'o', 'p'}),
                                       })));
   for (auto attempt = 0U; attempt < 20U && server.metrics().dht_queries == 0U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{10}, "rejected DHT value request");
   }
   stream.cancel();
   BOOST_TEST(server.metrics().dht_queries == 1U);
   BOOST_TEST(server.routing_status(builtins::kad_dht).active == 0U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_inbound_query_does_not_admit_client_as_server) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = dht_options_for(peer(231), amino_v1(dht::mode::server));
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, options_for(peer(232), capability_set{.bits = capabilities::direct_quic})};
   const auto server_endpoint = listen(server, runtime);

   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::kad_dht));
   const auto request = dht::message{
       .type = dht::message_type::find_node,
       .key_value = make_dht_key(client.local_peer()),
   };
   forge::asio::blocking::run(runtime, stream.async_write(dht::codec::encode(request)));
   const auto response =
       dht::codec::decode(wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))));

   BOOST_TEST(static_cast<int>(response.type) == static_cast<int>(request.type));
   BOOST_TEST(response.key_value.bytes == request.key_value.bytes, boost::test_tools::per_element());
   BOOST_TEST(server.routing_status(builtins::kad_dht).active == 0U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_rejected_request_cannot_inject_routing_candidates) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = dht_options_for(peer(233), amino_v1(dht::mode::server));
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, options_for(peer(234), capability_set{.bits = capabilities::direct_quic})};
   const auto server_endpoint = listen(server, runtime);
   const auto injected = peer(235);
   auto injected_endpoint = make_dns_tcp_endpoint(4235, "injected.example.com");
   injected_endpoint.peer = injected;

   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::kad_dht));
   forge::asio::blocking::run(runtime,
                              stream.async_write(dht::codec::encode(dht::message{
                                  .type = dht::message_type::get_value,
                                  .key_value = make_dht_key(std::vector<std::uint8_t>{'r', 'e', 'j', 'e', 'c', 't'}),
                                  .closer_peers = std::vector<dht::peer>{dht::peer{
                                      .id = injected,
                                      .endpoints = std::vector<endpoint>{injected_endpoint},
                                      .connection = dht::connection_type::can_connect,
                                  }},
                              })));
   for (auto attempt = 0U; attempt < 20U && server.metrics().dht_queries == 0U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{10}, "rejected DHT candidate injection");
   }
   stream.cancel();

   BOOST_TEST(server.metrics().dht_queries == 1U);
   BOOST_TEST(!server.peers().find(injected).has_value());
   BOOST_TEST(server.routing_status(builtins::kad_dht).active == 0U);
   BOOST_TEST(server.routing_status(builtins::kad_dht).replacements == 0U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_discovery_rejects_third_party_non_routable_endpoints) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options =
       options_for(peer(226), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   auto client_options =
       options_for(peer(227), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   const auto public_identity = make_test_certificate_identity("rendezvous-third-party-public");
   const auto relay_identity = make_test_certificate_identity("rendezvous-third-party-relay");
   const auto loopback_identity = make_test_certificate_identity("rendezvous-third-party-loopback");
   const auto private_identity = make_test_certificate_identity("rendezvous-third-party-private");
   const auto loopback_relay_identity = make_test_certificate_identity("rendezvous-third-party-loopback-relay");
   const auto private_relay_identity = make_test_certificate_identity("rendezvous-third-party-private-relay");
   auto public_endpoint = make_dns_tcp_endpoint(4228, "api.example.com");
   public_endpoint.peer = public_identity.peer;
   const auto relay_endpoint =
       parse_endpoint("/ip4/8.8.8.8/tcp/4001/p2p-circuit/p2p/" + relay_identity.peer.to_string());
   auto loopback_endpoint = make_tcp_endpoint(4230, "127.0.0.1");
   loopback_endpoint.peer = loopback_identity.peer;
   auto private_endpoint = make_tcp_endpoint(4231, "10.20.30.40");
   private_endpoint.peer = private_identity.peer;
   const auto loopback_relay_endpoint =
       parse_endpoint("/ip4/127.0.0.1/tcp/4001/p2p-circuit/p2p/" + loopback_relay_identity.peer.to_string());
   const auto private_relay_endpoint =
       parse_endpoint("/ip4/10.20.30.40/tcp/4001/p2p-circuit/p2p/" + private_relay_identity.peer.to_string());
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};

   forge::asio::blocking::run(runtime, server.peers().async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.discovery",
                                           .peer = public_identity.peer,
                                           .endpoints = std::vector<endpoint>{public_endpoint},
                                           .signed_peer_record = make_signed_rendezvous_peer_record(
                                               public_identity, std::vector<endpoint>{public_endpoint}, 1),
                                           .ttl = std::chrono::seconds{7'200},
                                           .expires_at = expires_at,
                                           .sequence = 1,
                                       }));
   forge::asio::blocking::run(runtime, server.peers().async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.discovery",
                                           .peer = relay_identity.peer,
                                           .endpoints = std::vector<endpoint>{relay_endpoint},
                                           .signed_peer_record = make_signed_rendezvous_peer_record(
                                               relay_identity, std::vector<endpoint>{relay_endpoint}, 2),
                                           .ttl = std::chrono::seconds{7'200},
                                           .expires_at = expires_at,
                                           .sequence = 2,
                                       }));
   forge::asio::blocking::run(runtime, server.peers().async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.discovery",
                                           .peer = loopback_identity.peer,
                                           .endpoints = std::vector<endpoint>{loopback_endpoint},
                                           .signed_peer_record = make_signed_rendezvous_peer_record(
                                               loopback_identity, std::vector<endpoint>{loopback_endpoint}, 3),
                                           .ttl = std::chrono::seconds{7'200},
                                           .expires_at = expires_at,
                                           .sequence = 3,
                                       }));
   forge::asio::blocking::run(runtime, server.peers().async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.discovery",
                                           .peer = private_identity.peer,
                                           .endpoints = std::vector<endpoint>{private_endpoint},
                                           .signed_peer_record = make_signed_rendezvous_peer_record(
                                               private_identity, std::vector<endpoint>{private_endpoint}, 4),
                                           .ttl = std::chrono::seconds{7'200},
                                           .expires_at = expires_at,
                                           .sequence = 4,
                                       }));
   forge::asio::blocking::run(runtime,
                              server.peers().async_upsert_rendezvous(rendezvous::registration{
                                  .namespace_name = "forge.discovery",
                                  .peer = loopback_relay_identity.peer,
                                  .endpoints = std::vector<endpoint>{loopback_relay_endpoint},
                                  .signed_peer_record = make_signed_rendezvous_peer_record(
                                      loopback_relay_identity, std::vector<endpoint>{loopback_relay_endpoint}, 5),
                                  .ttl = std::chrono::seconds{7'200},
                                  .expires_at = expires_at,
                                  .sequence = 5,
                              }));
   forge::asio::blocking::run(runtime,
                              server.peers().async_upsert_rendezvous(rendezvous::registration{
                                  .namespace_name = "forge.discovery",
                                  .peer = private_relay_identity.peer,
                                  .endpoints = std::vector<endpoint>{private_relay_endpoint},
                                  .signed_peer_record = make_signed_rendezvous_peer_record(
                                      private_relay_identity, std::vector<endpoint>{private_relay_endpoint}, 6),
                                  .ttl = std::chrono::seconds{7'200},
                                  .expires_at = expires_at,
                                  .sequence = 6,
                              }));

   const auto discovered = forge::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .limit = 10,
                                                                      }));
   auto peers = std::set<std::string>{};
   for (const auto& registration : discovered.registrations) {
      peers.insert(registration.peer.to_string());
   }
   BOOST_TEST(peers.contains(public_identity.peer.to_string()));
   BOOST_TEST(peers.contains(relay_identity.peer.to_string()));
   BOOST_TEST(!peers.contains(loopback_identity.peer.to_string()));
   BOOST_TEST(!peers.contains(private_identity.peer.to_string()));
   BOOST_TEST(!peers.contains(loopback_relay_identity.peer.to_string()));
   BOOST_TEST(!peers.contains(private_relay_identity.peer.to_string()));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_stop_closes_all_direct_listeners) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(216))};
   forge::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   forge::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   const auto local = server.local_endpoints();
   const auto quic = require_endpoint_for(local, endpoint::protocol_kind::quic_v1);
   const auto tcp = require_endpoint_for(local, endpoint::protocol_kind::tcp);

   forge::asio::blocking::run(runtime, server.async_stop());

   auto rebound = node{runtime, options_for(peer(217))};
   forge::asio::blocking::run(runtime, rebound.async_listen(quic));
   auto tcp_rebound = forge::net::tcp::listener{runtime.context().get_executor(), tcp.transport};
   BOOST_TEST(tcp_rebound.valid());

   forge::asio::blocking::run(runtime, tcp_rebound.async_close());
   forge::asio::blocking::run(runtime, rebound.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_nodes_prefer_tls_yamux_and_echo_frames) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_identity();
   const auto client_identity = make_test_identity();
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};
   register_echo(server);

   const auto server_endpoint = listen_tcp(server, runtime);
   BOOST_TEST(server_endpoint.is_direct_tcp());

   const auto session = forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo));
   const auto payload = std::vector<std::uint8_t>{'t', 'c', 'p'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);
   BOOST_TEST(server.metrics().protocol_streams_accepted >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_accept_survives_aborted_inbound_upgrade) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_identity();
   const auto client_identity = make_test_identity();
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};
   register_echo(server);
   const auto server_endpoint = listen_tcp(server, runtime);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto socket = boost::asio::ip::tcp::socket{co_await boost::asio::this_coro::executor};
      co_await socket.async_connect(
          boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address(server_endpoint.transport.host),
                                         server_endpoint.transport.port},
          boost::asio::use_awaitable);
      auto ignored = boost::system::error_code{};
      socket.close(ignored);
   }());
   const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (server.metrics().handshakes_failed == 0U) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < failure_deadline);
      wait_on_runtime(runtime, std::chrono::milliseconds{1}, "aborted inbound upgrade");
   }

   const auto session = forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(session.remote_peer.to_string() == server.local_peer().to_string());
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'a', 'f', 't', 'e', 'r'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_concurrent_handshakes_reuse_configured_node_identity) {
   constexpr auto connection_count = std::size_t{4};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = connection_count * 2}};
   const auto client_identity = make_test_identity();
   auto client = node{runtime, options_for(client_identity)};
   auto servers = std::vector<std::unique_ptr<node>>{};
   auto endpoints = std::vector<endpoint>{};
   servers.reserve(connection_count);
   endpoints.reserve(connection_count);

   for (auto index = std::size_t{}; index < connection_count; ++index) {
      const auto server_identity = make_test_identity();
      auto server = std::make_unique<node>(runtime, options_for(server_identity));
      register_echo(*server);
      endpoints.push_back(listen_tcp(*server, runtime));
      servers.push_back(std::move(server));
   }

   auto ready = std::atomic_size_t{};
   auto connections = std::vector<std::future<node::session_info>>{};
   connections.reserve(connection_count);
   for (auto index = std::size_t{}; index < connection_count; ++index) {
      connections.push_back(boost::asio::co_spawn(
          runtime.context(),
          [&client, &servers, &endpoints, &ready, index]() -> boost::asio::awaitable<node::session_info> {
             ready.fetch_add(1, std::memory_order_acq_rel);
             while (ready.load(std::memory_order_acquire) != connection_count) {
                co_await boost::asio::post(boost::asio::use_awaitable);
             }
             co_return co_await client.async_connect(endpoints[index],
                                                     node::connect_options{
                                                         .expected_peer = servers[index]->local_peer(),
                                                         .allow_relay = false,
                                                     });
          },
          boost::asio::use_future));
   }

   for (auto index = std::size_t{}; index < connection_count; ++index) {
      BOOST_REQUIRE(connections[index].wait_for(std::chrono::seconds{5}) == std::future_status::ready);
      const auto session = connections[index].get();
      BOOST_TEST(session.remote_peer.value == servers[index]->local_peer().value);

      auto stream = forge::asio::blocking::run(
          runtime, client.async_open_protocol_stream(servers[index]->local_peer(), builtins::echo,
                                                     node::open_options{.allow_relay = false}));
      const auto payload = std::vector<std::uint8_t>{'t', 'l', 's', static_cast<std::uint8_t>(index)};
      forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
      const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
      BOOST_TEST(reply == payload, boost::test_tools::per_element());
   }

   BOOST_TEST(client.metrics().active_sessions == connection_count);
   forge::asio::blocking::run(runtime, client.async_stop());
   for (auto& server : servers) {
      forge::asio::blocking::run(runtime, server->async_stop());
   }
}

BOOST_AUTO_TEST_CASE(p2p_insecure_quic_node_does_not_require_signing_identity) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto options = options_for(peer(218));
   options.certificate_pem.clear();
   options.private_key_pem.clear();
   options.public_key.clear();

   auto value = node{runtime, std::move(options)};
   BOOST_TEST(value.local_peer().value == peer(218).value);
   forge::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_rejects_tls_peer_mismatch) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("tcp-peer-mismatch-server");
   const auto client_identity = make_test_certificate_identity("tcp-peer-mismatch-client");
   auto server_options = options_for(server_identity);
   auto client_options = options_for(client_identity);
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_state.persistence = peer_store::make_memory_persistence();
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto server_endpoint = listen_tcp(server, runtime);
   auto rejected = boost::asio::co_spawn(
       runtime.context(), client.async_connect(server_endpoint, node::connect_options{.expected_peer = peer(150)}),
       boost::asio::use_future);
   BOOST_REQUIRE(rejected.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   try {
      static_cast<void>(rejected.get());
      BOOST_FAIL("expected TCP TLS peer mismatch");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   auto stopped = boost::asio::co_spawn(runtime.context(), client.async_stop(), boost::asio::use_future);
   BOOST_REQUIRE(stopped.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   stopped.get();
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_upgrade_honors_attempt_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(peer(201))};
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime);

   auto saw_timeout = false;
   const auto completed = forge::asio::blocking::run_for(
       runtime,
       [&]() -> boost::asio::awaitable<void> {
          try {
             (void)co_await client.async_connect(stalled_endpoint,
                                                 node::connect_options{.expected_peer = peer(202),
                                                                       .allow_relay = false,
                                                                       .timeout = std::chrono::milliseconds{100}});
             BOOST_FAIL("expected stalled TCP direct connect timeout");
          } catch (const forge::exceptions::base& error) {
             BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
             BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                        static_cast<int>(exceptions::code::timeout));
             saw_timeout = true;
          }
       }(),
       std::chrono::milliseconds{1'000});

   BOOST_TEST(completed);
   BOOST_TEST(saw_timeout);
   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_noise_stall_honors_attempt_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(make_test_identity())};
   auto security_started = std::make_shared<std::promise<void>>();
   auto security_started_future = security_started->get_future();
   const auto stalled_endpoint =
       start_stalling_security_tcp_peer(runtime, protocol_id{.value = "/noise"}, security_started);

   auto pending = boost::asio::co_spawn(runtime.context(),
                                        client.async_connect(stalled_endpoint,
                                                             node::connect_options{
                                                                 .expected_peer = peer(202),
                                                                 .allow_relay = false,
                                                                 .timeout = std::chrono::milliseconds{100},
                                                             }),
                                        boost::asio::use_future);
   wait_for_server(security_started_future, std::chrono::seconds{2}, "Noise handshake stall");

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   try {
      (void)pending.get();
      BOOST_FAIL("expected stalled Noise handshake timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::timeout));
   }
   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_noise_stall_is_canceled_by_node_stop) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(make_test_identity())};
   auto security_started = std::make_shared<std::promise<void>>();
   auto security_started_future = security_started->get_future();
   const auto stalled_endpoint =
       start_stalling_security_tcp_peer(runtime, protocol_id{.value = "/noise"}, security_started);
   auto pending = boost::asio::co_spawn(runtime.context(),
                                        client.async_connect(stalled_endpoint,
                                                             node::connect_options{
                                                                 .expected_peer = peer(203),
                                                                 .allow_relay = false,
                                                                 .timeout = std::chrono::seconds{5},
                                                             }),
                                        boost::asio::use_future);
   wait_for_server(security_started_future, std::chrono::seconds{2}, "Noise handshake stop stall");

   const auto started = std::chrono::steady_clock::now();
   forge::asio::blocking::run(runtime, client.async_stop());
   const auto stop_elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
   BOOST_TEST(stop_elapsed < 750);
   BOOST_REQUIRE(pending.wait_for(std::chrono::milliseconds{750}) == std::future_status::ready);
   try {
      (void)pending.get();
      BOOST_FAIL("expected stalled Noise handshake cancellation");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      const auto kind = *forge::net::p2p::exceptions::code_of(error);
      const auto stopped = kind == exceptions::code::canceled || kind == exceptions::code::closed;
      BOOST_TEST_CONTEXT("stop error=" << static_cast<int>(kind)) {
         BOOST_TEST(stopped);
      }
   }
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_tls_stall_honors_attempt_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(make_test_identity())};
   auto security_started = std::make_shared<std::promise<void>>();
   auto security_started_future = security_started->get_future();
   const auto stalled_endpoint =
       start_stalling_security_tcp_peer(runtime, protocol_id{.value = "/tls/1.0.0"}, security_started);

   auto pending = boost::asio::co_spawn(runtime.context(),
                                        client.async_connect(stalled_endpoint,
                                                             node::connect_options{
                                                                 .expected_peer = peer(204),
                                                                 .allow_relay = false,
                                                                 .timeout = std::chrono::milliseconds{100},
                                                             }),
                                        boost::asio::use_future);
   wait_for_server(security_started_future, std::chrono::seconds{2}, "TLS handshake stall");

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   try {
      (void)pending.get();
      BOOST_FAIL("expected stalled TLS handshake timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::timeout));
   }
   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_stop_cancels_shared_tls_owner_during_yamux_negotiation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 3}};
   const auto server_identity = make_test_certificate_identity("tls-yamux-stall-server");
   const auto client_identity = make_test_certificate_identity("tls-yamux-stall-client");
   auto server_identity_options = options_for(server_identity);
   auto client_options = options_for(client_identity);
   client_options.allow_insecure_test_mode = false;
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto client = node{runtime, std::move(client_options)};
   auto yamux_started = std::make_shared<std::promise<void>>();
   auto yamux_started_future = yamux_started->get_future();
   auto peer_finished = std::make_shared<std::promise<void>>();
   auto peer_finished_future = peer_finished->get_future();
   auto stalled_endpoint =
       start_stalling_tls_yamux_tcp_peer(runtime, server_identity_options, yamux_started, peer_finished);
   stalled_endpoint.peer = server_identity.peer;

   auto pending = boost::asio::co_spawn(runtime.context(),
                                        client.async_connect(stalled_endpoint,
                                                             node::connect_options{
                                                                 .expected_peer = server_identity.peer,
                                                                 .allow_relay = false,
                                                                 .timeout = std::chrono::seconds{5},
                                                             }),
                                        boost::asio::use_future);
   wait_for_server(yamux_started_future, std::chrono::seconds{2}, "TLS Yamux negotiation stall");

   auto stopped = boost::asio::co_spawn(runtime.context(), client.async_stop(), boost::asio::use_future);
   BOOST_REQUIRE(stopped.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   stopped.get();
   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   try {
      static_cast<void>(pending.get());
      BOOST_FAIL("node stop should cancel stalled Yamux negotiation through the shared TLS owner");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      const auto kind = *forge::net::p2p::exceptions::code_of(error);
      const auto canceled = kind == exceptions::code::canceled || kind == exceptions::code::closed;
      BOOST_TEST(canceled);
   }
   BOOST_REQUIRE(peer_finished_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   peer_finished_future.get();
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_stop_closes_listener_port) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto server = node{runtime, options_for(peer(203))};
   const auto server_endpoint = listen_tcp(server, runtime);

   forge::asio::blocking::run(runtime, server.async_stop());

   auto rebound = forge::net::tcp::listener{runtime.context().get_executor(), server_endpoint.transport};
   BOOST_TEST(rebound.valid());
   forge::asio::blocking::run(runtime, rebound.async_close());
}

BOOST_AUTO_TEST_CASE(quic_libp2p_profile_sets_required_alpn) {
   auto client = forge::net::quic::libp2p::client_profile();
   auto server = forge::net::quic::libp2p::server_profile();

   BOOST_TEST(client.alpn == "libp2p");
   BOOST_TEST(server.alpn == "libp2p");
   BOOST_TEST(forge::net::quic::libp2p::is_profile_alpn(client.alpn));
}

BOOST_AUTO_TEST_CASE(transport_frame_and_stream_round_trip_payload) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(42);
   auto value = forge::net::transport::detail::stream_access::make(backend);
   const auto payload = std::vector<std::uint8_t>{'t', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't'};
   const auto encoded = forge::net::transport::encode_frame(payload);
   const auto decoded = forge::net::transport::decode_frame(encoded);
   BOOST_TEST(static_cast<int>(decoded.status) ==
              static_cast<int>(forge::net::transport::frame_decode_status::complete));
   BOOST_TEST(decoded.payload == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, value.async_write_frame(payload));
   BOOST_REQUIRE_EQUAL(backend->writes.size(), 1U);
   BOOST_TEST(forge::net::transport::decode_frame(backend->writes.front()).payload == payload,
              boost::test_tools::per_element());

   backend->reads.push_back({encoded.begin(), encoded.begin() + 3});
   backend->reads.push_back({encoded.begin() + 3, encoded.end()});
   const auto read = forge::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_TEST(read == payload, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(p2p_stream_wraps_transport_stream) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(77);
   auto value = forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(backend)};
   const auto payload = std::vector<std::uint8_t>{'p', '2', 'p'};
   const auto reply = std::vector<std::uint8_t>{'o', 'k'};

   BOOST_TEST(value.valid());
   BOOST_TEST(value.id() == 77);

   forge::asio::blocking::run(runtime, value.async_write(payload));
   BOOST_REQUIRE_EQUAL(backend->writes.size(), 1U);
   BOOST_TEST(backend->writes.front() == payload, boost::test_tools::per_element());

   backend->reads.push_back(reply);
   const auto read = forge::asio::blocking::run(runtime, value.async_read());
   BOOST_TEST(read == reply, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, value.async_close());
   BOOST_TEST(backend->closed);
}

BOOST_AUTO_TEST_CASE(p2p_stream_delegates_chunk_read_write_and_preserves_framed_trailing_bytes) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(78);
   auto value = forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(backend)};
   const auto payload = std::vector<std::uint8_t>{'c', 'h', 'u', 'n', 'k'};
   const auto first = std::vector<std::uint8_t>{'o', 'n', 'e'};
   const auto second = std::vector<std::uint8_t>{'t', 'w', 'o'};

   forge::asio::blocking::run(runtime, value.async_write(forge::net::transport::chunk{payload}));
   BOOST_REQUIRE_EQUAL(backend->chunk_writes, 1U);
   BOOST_REQUIRE_EQUAL(backend->writes.size(), 1U);
   BOOST_TEST(backend->writes.front() == payload, boost::test_tools::per_element());

   backend->reads.push_back(payload);
   auto read = forge::asio::blocking::run(runtime, value.async_read_chunk());
   BOOST_REQUIRE_EQUAL(backend->chunk_reads, 1U);
   BOOST_TEST(read.to_vector() == payload, boost::test_tools::per_element());

   auto combined = forge::net::transport::encode_frame(first);
   auto encoded_second = forge::net::transport::encode_frame(second);
   combined.insert(combined.end(), encoded_second.begin(), encoded_second.end());
   backend->reads.push_back(std::move(combined));

   auto first_read = forge::asio::blocking::run(runtime, value.async_read_frame_chunk());
   BOOST_TEST(first_read.to_vector() == first, boost::test_tools::per_element());
   const auto second_read = forge::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_TEST(second_read == second, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(backend->chunk_reads, 2U);
}

BOOST_AUTO_TEST_CASE(p2p_stream_with_buffer_preserves_prefetched_framed_chunks) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(79);
   auto first = std::vector<std::uint8_t>{'p', 'r', 'e'};
   auto second = std::vector<std::uint8_t>{'b', 'u', 'f'};
   auto buffered = forge::net::transport::encode_frame(first);
   auto encoded_second = forge::net::transport::encode_frame(second);
   buffered.insert(buffered.end(), encoded_second.begin(), encoded_second.end());

   auto value = forge::net::p2p::detail::stream_access::with_buffer(
       forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(backend)}, std::move(buffered));

   auto first_read = forge::asio::blocking::run(runtime, value.async_read_frame_chunk());
   BOOST_TEST(first_read.to_vector() == first, boost::test_tools::per_element());
   const auto second_read = forge::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_TEST(second_read == second, boost::test_tools::per_element());
   BOOST_TEST(backend->chunk_reads == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_session_lifecycle_ignores_stale_replaced_session) {
   struct tracked_session {
      std::uint64_t id = 0;
      node::session_info info;
   };

   const auto remote = peer(2);
   auto sessions = std::map<std::uint64_t, std::shared_ptr<tracked_session>>{};
   auto stale = std::make_shared<tracked_session>(tracked_session{.id = 1, .info = {.remote_peer = remote}});
   auto current = std::make_shared<tracked_session>(tracked_session{.id = 2, .info = {.remote_peer = remote}});

   sessions[current->id] = current;

   BOOST_TEST(!detail::erase_current_session(sessions, stale));
   BOOST_REQUIRE(sessions.contains(current->id));
   BOOST_TEST(sessions.at(current->id).get() == current.get());

   BOOST_TEST(detail::erase_current_session(sessions, current));
   BOOST_TEST(!sessions.contains(current->id));
}

BOOST_AUTO_TEST_CASE(p2p_session_lifecycle_cancels_rejected_new_session) {
   struct tracked_connection {
      void cancel() {
         canceled = true;
         ++cancel_count;
      }

      bool canceled = false;
      std::size_t cancel_count = 0;
   };
   struct tracked_session {
      bool closed = false;
      tracked_connection connection;
   };

   auto rejected = std::make_shared<tracked_session>();
   detail::cancel_rejected_session(rejected);

   BOOST_TEST(rejected->closed);
   BOOST_TEST(rejected->connection.canceled);
   BOOST_TEST(rejected->connection.cancel_count == 1U);
}

BOOST_AUTO_TEST_CASE(p2p_inbound_stop_between_accept_and_admission_is_not_a_handshake_failure) {
   auto handshakes_failed = std::uint64_t{};
   auto wire_frames = std::vector<std::vector<std::uint8_t>>{};

   for (const auto kind : {exceptions::code::closed, exceptions::code::canceled}) {
      if (!detail::suppress_inbound_handshake_failure(kind, true)) {
         ++handshakes_failed;
         wire_frames.push_back({'f', 'a', 'i', 'l'});
      }
   }

   BOOST_TEST(handshakes_failed == 0U);
   BOOST_TEST(wire_frames.empty());
   BOOST_TEST(!detail::suppress_inbound_handshake_failure(exceptions::code::closed, false));
   BOOST_TEST(!detail::suppress_inbound_handshake_failure(exceptions::code::peer_verification_failed, true));
}

BOOST_AUTO_TEST_CASE(quic_transport_adapter_preserves_endpoint_kind_and_authority) {
   const auto ip4 =
       forge::net::quic::to_transport_endpoint(forge::net::quic::endpoint{.host = "127.0.0.1", .port = 4001});
   BOOST_TEST(static_cast<int>(ip4.host_type) == static_cast<int>(forge::net::transport::endpoint::host_kind::ip4));
   BOOST_TEST(static_cast<int>(ip4.protocol) ==
              static_cast<int>(forge::net::transport::endpoint::protocol_kind::quic_v1));
   const auto authority = std::string{"127.0.0.1"} + ":" + std::to_string(4001);
   BOOST_TEST(ip4.authority() == authority);
   const auto roundtrip = forge::net::quic::from_transport_endpoint(ip4);
   BOOST_TEST(roundtrip.authority() == authority);

   const auto dns =
       forge::net::quic::to_transport_endpoint(forge::net::quic::endpoint{.host = "localhost", .port = 4002});
   BOOST_TEST(static_cast<int>(dns.host_type) == static_cast<int>(forge::net::transport::endpoint::host_kind::dns));
}

BOOST_AUTO_TEST_CASE(p2p_multistream_select_encodes_libp2p_messages) {
   using namespace protocol_negotiation;

   const auto header = encode_frame(encode_message(protocol_negotiation::message{.kind = message_kind::header}));
   BOOST_TEST(header == std::vector<std::uint8_t>({19,  '/', 'm', 'u', 'l', 't', 'i', 's', 't', 'r',
                                                   'e', 'a', 'm', '/', '1', '.', '0', '.', '0', '\n'}),
              boost::test_tools::per_element());

   const auto ping = protocol_id{.value = "/ipfs/ping/1.0.0"};
   auto decoded = decode_message(decode_frame(encode_frame(encode_message(protocol_negotiation::message{
                                                  .kind = message_kind::protocol, .protocol = ping})))
                                     .payload);
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(message_kind::protocol));
   BOOST_TEST(decoded.protocol.value == ping.value);

   auto list = decode_message(encode_message(
       protocol_negotiation::message{.kind = message_kind::protocols, .protocols = std::vector<protocol_id>{ping}}));
   BOOST_TEST(static_cast<int>(list.kind) == static_cast<int>(message_kind::protocols));
   BOOST_REQUIRE_EQUAL(list.protocols.size(), 1U);
   BOOST_TEST(list.protocols.front().value == ping.value);

   BOOST_CHECK_THROW((void)decode_frame(std::vector<std::uint8_t>{0x81, 0x81, 0x01}), forge::exceptions::base);
   BOOST_CHECK_THROW((void)decode_message(std::vector<std::uint8_t>{'b', 'a', 'd', '\n'}), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_reachability_relay_protocol_ids_are_exact) {
   BOOST_TEST(builtins::autonat_v1.value == "/libp2p/autonat/1.0.0");
   BOOST_TEST(builtins::autonat_v2_dial_request.value == "/libp2p/autonat/2/dial-request");
   BOOST_TEST(builtins::autonat_v2_dial_back.value == "/libp2p/autonat/2/dial-back");
   BOOST_TEST(builtins::relay_hop.value == "/libp2p/circuit/relay/0.2.0/hop");
   BOOST_TEST(builtins::relay_stop.value == "/libp2p/circuit/relay/0.2.0/stop");
   BOOST_TEST(builtins::dcutr.value == "/libp2p/dcutr");
   BOOST_TEST(builtins::kad_dht.value == "/ipfs/kad/1.0.0");
   BOOST_TEST(builtins::rendezvous.value == "/rendezvous/1.0.0");
   BOOST_TEST(builtins::meshsub_v11.value == "/meshsub/1.1.0");
   BOOST_TEST(builtins::meshsub_v10.value == "/meshsub/1.0.0");
}

BOOST_AUTO_TEST_CASE(p2p_reachability_relay_public_types_are_owner_scoped) {
   static_assert(std::is_same_v<decltype(reachability::result{}.value), reachability::state>);
   static_assert(std::is_same_v<decltype(hole_punch::result{}.value), hole_punch::status>);
   static_assert(std::is_same_v<decltype(relay::reservation::info{}.relay_peer), peer_id>);
   static_assert(std::is_same_v<decltype(path::policy{}.allow_relay), bool>);
   static_assert(std::is_same_v<decltype(path::result{}.kind), path::kind>);
   static_assert(std::is_same_v<decltype(resource_manager::snapshot{}.active_streams), std::size_t>);
   static_assert(std::is_same_v<decltype(dht::options{}.replication), std::size_t>);
   static_assert(std::is_same_v<decltype(dht::query_result{}.target), dht::key>);
   static_assert(std::is_same_v<decltype(rendezvous::registration{}.peer), peer_id>);
   static_assert(std::is_same_v<decltype(discovery::policy{}.enabled), bool>);
   static_assert(std::is_same_v<decltype(discovery::result{}.discovered_by), discovery::source>);
}

BOOST_AUTO_TEST_CASE(p2p_dht_codec_roundtrips_libp2p_message_shape_and_rejects_malformed) {
   const auto profile = amino_v1();
   const auto provider = peer(90);
   const auto target = peer(91);
   const auto provider_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4900/quic-v1/p2p/" + provider.to_string());
   const auto target_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4901/quic-v1/p2p/" + target.to_string());
   const auto key = amino_provider_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't'});

   const auto encoded = dht::codec::encode(
       dht::message{
           .type = dht::message_type::get_providers,
           .key_value = key,
           .closer_peers = std::vector<dht::peer>{dht::peer{
               .id = target,
               .endpoints = std::vector<endpoint>{target_endpoint},
               .connection = dht::connection_type::can_connect,
           }},
           .provider_peers = std::vector<dht::peer>{dht::peer{
               .id = provider,
               .endpoints = std::vector<endpoint>{provider_endpoint},
               .connection = dht::connection_type::connected,
           }},
       },
       profile);
   const auto decoded = dht::codec::decode(encoded, profile);

   BOOST_TEST(static_cast<int>(decoded.type) == static_cast<int>(dht::message_type::get_providers));
   BOOST_TEST(decoded.key_value.bytes == key.bytes, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(decoded.closer_peers.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.provider_peers.size(), 1U);
   BOOST_TEST(decoded.closer_peers.front().id.to_string() == target.to_string());
   BOOST_TEST(decoded.closer_peers.front().endpoints.front().to_string() == target_endpoint.to_string());
   BOOST_TEST(static_cast<int>(decoded.closer_peers.front().connection) ==
              static_cast<int>(dht::connection_type::can_connect));
   BOOST_TEST(decoded.provider_peers.front().id.to_string() == provider.to_string());
   BOOST_TEST(decoded.provider_peers.front().endpoints.front().to_string() == provider_endpoint.to_string());
   BOOST_TEST(static_cast<int>(decoded.provider_peers.front().connection) ==
              static_cast<int>(dht::connection_type::connected));

   BOOST_CHECK_THROW((void)dht::codec::decode(std::vector<std::uint8_t>{0x02, 0x08, 0x63}, profile),
                     forge::exceptions::base);
   BOOST_CHECK_THROW((void)dht::codec::decode(std::vector<std::uint8_t>{0x01, 0x10}, profile), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_dht_exchange_rejects_mismatched_response_before_admission) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto profile = amino_v1();
   const auto request = dht::message{
       .type = dht::message_type::find_node,
       .key_value = make_dht_key(std::vector<std::uint8_t>{'r', 'e', 'q', 'u', 'e', 's', 't'}),
   };
   auto backend = std::make_shared<queued_transport_stream>(51);
   backend->reads.push_back(dht::codec::encode(dht::message{
       .type = dht::message_type::find_node,
       .key_value = make_dht_key(std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'}),
   }));
   auto stream = forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(backend)};

   try {
      (void)forge::asio::blocking::run(runtime,
                                       detail::async_exchange_dht(std::move(stream), request, profile,
                                                                  runtime.context(), std::chrono::milliseconds{250}));
      BOOST_FAIL("expected mismatched DHT response rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) == static_cast<int>(exceptions::code::protocol_error));
   }
   BOOST_TEST(backend->closed);
}

BOOST_AUTO_TEST_CASE(p2p_dht_exchange_accepts_omitted_or_echoed_response_key) {
   const auto profile = amino_v1();
   const auto request = dht::message{
       .type = dht::message_type::find_node,
       .key_value = make_dht_key(std::vector<std::uint8_t>{'r', 'e', 'q', 'u', 'e', 's', 't'}),
   };

   BOOST_CHECK_NO_THROW(
       detail::validate_dht_response(request, dht::message{.type = dht::message_type::find_node}, profile));
   BOOST_CHECK_NO_THROW(detail::validate_dht_response(
       request, dht::message{.type = dht::message_type::find_node, .key_value = request.key_value}, profile));
}

BOOST_AUTO_TEST_CASE(p2p_dht_exchange_times_out_while_waiting_for_response) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto profile = amino_v1();
   auto backend = std::make_shared<stalling_transport_stream>(52);
   auto stream = forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(backend)};
   const auto started = std::chrono::steady_clock::now();

   try {
      (void)forge::asio::blocking::run(
          runtime, detail::async_exchange_dht(
                       std::move(stream),
                       dht::message{
                           .type = dht::message_type::find_node,
                           .key_value = make_dht_key(std::vector<std::uint8_t>{'t', 'i', 'm', 'e', 'o', 'u', 't'}),
                       },
                       profile, runtime.context(), std::chrono::milliseconds{25}));
      BOOST_FAIL("expected DHT exchange timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) == static_cast<int>(exceptions::code::timeout));
   }
   BOOST_TEST(backend->closed);
   BOOST_TEST(
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() <
       1'000);
}

BOOST_AUTO_TEST_CASE(p2p_dht_k_bucket_bounds_active_and_replacement_capacity) {
   const auto local = peer(92);
   const auto peers = peers_in_same_dht_bucket(local, 5);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(
                  dht::mode::client,
                  dht::options{.replication = 2, .alpha = 1, .replacement_cache_size = 2, .failure_threshold = 2})
                  .limits};
   for (const auto& candidate : peers) {
      table.upsert(dht::peer{.id = candidate}, dht::routing_admission::verified_server);
   }
   table.upsert(dht::peer{.id = local});

   const auto status = table.status();
   BOOST_TEST(status.active == 2U);
   BOOST_TEST(status.replacements == 2U);
   BOOST_TEST(status.candidates == 0U);
   BOOST_TEST(status.nonempty_buckets == 1U);
   BOOST_REQUIRE_EQUAL(table.snapshot().size(), 2U);
   BOOST_REQUIRE_EQUAL(table.closest(peers.front().to_bytes(), 20).size(), 2U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_k_bucket_keeps_candidates_out_of_active_routing) {
   const auto local = peer(93);
   const auto peers = peers_in_same_dht_bucket(local, 2);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(dht::mode::client,
                                      dht::options{.replication = 2, .alpha = 1, .replacement_cache_size = 2})
                  .limits};
   table.upsert(dht::peer{.id = peers[0]}, dht::routing_admission::candidate);
   table.upsert(dht::peer{.id = peers[1]}, dht::routing_admission::verified_server);

   const auto status = table.status();
   BOOST_TEST(status.active == 1U);
   BOOST_TEST(status.replacements == 1U);
   BOOST_TEST(status.candidates == 1U);
   const auto active = table.snapshot();
   BOOST_REQUIRE_EQUAL(active.size(), 1U);
   BOOST_TEST(active.front().id.to_string() == peers[1].to_string());

   table.upsert(dht::peer{.id = peers[0]}, dht::routing_admission::verified_server);
   BOOST_TEST(table.status().active == 2U);
   BOOST_TEST(table.status().replacements == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_k_bucket_candidate_updates_cannot_downgrade_verified_entries) {
   const auto local = peer(236);
   const auto peers = peers_in_same_dht_bucket(local, 2);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(
                  dht::mode::client,
                  dht::options{.replication = 1, .alpha = 1, .replacement_cache_size = 1, .failure_threshold = 1})
                  .limits};
   auto active_endpoint = make_dns_tcp_endpoint(4236, "active.example.com");
   active_endpoint.peer = peers[0];
   auto replacement_endpoint = make_dns_tcp_endpoint(4237, "replacement.example.com");
   replacement_endpoint.peer = peers[1];
   table.upsert(dht::peer{.id = peers[0], .endpoints = {active_endpoint}}, dht::routing_admission::verified_server);
   table.upsert(dht::peer{.id = peers[1], .endpoints = {replacement_endpoint}},
                dht::routing_admission::verified_server);

   auto untrusted_endpoint = make_dns_tcp_endpoint(4238, "untrusted.example.com");
   untrusted_endpoint.peer = peers[0];
   table.upsert(dht::peer{.id = peers[0], .endpoints = {untrusted_endpoint}}, dht::routing_admission::candidate);
   untrusted_endpoint.peer = peers[1];
   table.upsert(dht::peer{.id = peers[1], .endpoints = {untrusted_endpoint}}, dht::routing_admission::candidate);

   auto active = table.snapshot();
   BOOST_REQUIRE_EQUAL(active.size(), 1U);
   BOOST_REQUIRE_EQUAL(active.front().endpoints.size(), 1U);
   BOOST_TEST(active.front().endpoints.front().to_string() == active_endpoint.to_string());

   table.mark_failure(peers[0]);
   active = table.snapshot();
   BOOST_REQUIRE_EQUAL(active.size(), 1U);
   BOOST_REQUIRE_EQUAL(active.front().endpoints.size(), 1U);
   BOOST_TEST(active.front().id.to_string() == peers[1].to_string());
   BOOST_TEST(active.front().endpoints.front().to_string() == replacement_endpoint.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_seeds_include_bounded_candidates_without_admitting_them) {
   const auto local = peer(237);
   const auto candidates = peers_in_same_dht_bucket(local, 3);
   const auto target = peer(238);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(dht::mode::client,
                                      dht::options{.replication = 2, .alpha = 1, .replacement_cache_size = 3})
                  .limits};
   for (const auto& candidate : candidates) {
      table.upsert(dht::peer{.id = candidate}, dht::routing_admission::candidate);
   }

   BOOST_TEST(table.closest(target.to_bytes(), 10).empty());
   const auto first = table.query_seeds(target.to_bytes(), 2);
   const auto second = table.query_seeds(target.to_bytes(), 2);
   BOOST_REQUIRE_EQUAL(first.size(), 2U);
   BOOST_REQUIRE_EQUAL(second.size(), first.size());
   for (auto index = std::size_t{}; index < first.size(); ++index) {
      BOOST_TEST(first[index].id.to_string() == second[index].id.to_string());
   }
   BOOST_TEST(table.status().active == 0U);
   BOOST_TEST(table.status().candidates == 3U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_seeds_prioritize_active_and_evict_failed_replacements) {
   const auto local = peer(18);
   const auto peers = peers_in_same_dht_bucket(local, 4);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(
                  dht::mode::client,
                  dht::options{.replication = 2, .alpha = 1, .replacement_cache_size = 2, .failure_threshold = 2})
                  .limits};
   table.upsert(dht::peer{.id = peers[0]}, dht::routing_admission::verified_server);
   table.upsert(dht::peer{.id = peers[1]}, dht::routing_admission::verified_server);
   table.upsert(dht::peer{.id = peers[2]}, dht::routing_admission::verified_server);
   table.upsert(dht::peer{.id = peers[3]}, dht::routing_admission::candidate);

   auto expected = std::vector<peer_id>{peers[0], peers[1]};
   std::ranges::sort(expected, [&](const auto& left, const auto& right) {
      const auto left_distance = distance_between(left.to_bytes(), peers[3].to_bytes());
      const auto right_distance = distance_between(right.to_bytes(), peers[3].to_bytes());
      return left_distance != right_distance ? left_distance < right_distance : left < right;
   });
   const auto seeds = table.query_seeds(peers[3].to_bytes(), 2);
   BOOST_REQUIRE_EQUAL(seeds.size(), 2U);
   BOOST_TEST(seeds[0].id.to_string() == expected[0].to_string());
   BOOST_TEST(seeds[1].id.to_string() == expected[1].to_string());

   for (const auto& replacement : {peers[2], peers[3]}) {
      table.mark_failure(replacement);
      table.mark_failure(replacement);
   }
   BOOST_TEST(table.status().replacements == 0U);
   BOOST_TEST(table.status().candidates == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_k_bucket_promotes_verified_replacement_before_candidate) {
   const auto local = peer(94);
   const auto peers = peers_in_same_dht_bucket(local, 3);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(
                  dht::mode::client,
                  dht::options{.replication = 1, .alpha = 1, .replacement_cache_size = 2, .failure_threshold = 1})
                  .limits};
   table.upsert(dht::peer{.id = peers[0]}, dht::routing_admission::verified_server);
   table.upsert(dht::peer{.id = peers[1]}, dht::routing_admission::candidate);
   table.upsert(dht::peer{.id = peers[2]}, dht::routing_admission::verified_server);
   table.mark_failure(peers[0]);

   const auto active = table.snapshot();
   BOOST_REQUIRE_EQUAL(active.size(), 1U);
   BOOST_TEST(active.front().id.to_string() == peers[2].to_string());
   BOOST_TEST(table.status().replacements == 1U);
   BOOST_TEST(table.status().candidates == 1U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_k_bucket_closest_is_sha256_xor_ordered_and_deterministic) {
   const auto local = peer(95);
   const auto candidates = std::vector<peer_id>{peer(96), peer(97), peer(98), peer(99)};
   const auto target = peer(100);
   auto table = dht::routing_table{
       local,
       custom_test_dht_profile(dht::mode::client, dht::options{.replication = candidates.size(), .alpha = 1}).limits};
   for (const auto& candidate : candidates) {
      table.upsert(dht::peer{.id = candidate});
   }

   const auto zero = distance_between(target.to_bytes(), target.to_bytes());
   BOOST_TEST(std::ranges::all_of(zero.bytes, [](auto value) { return value == 0; }));

   auto expected = candidates;
   std::ranges::sort(expected, [&](const auto& left, const auto& right) {
      const auto left_distance = distance_between(left.to_bytes(), target.to_bytes());
      const auto right_distance = distance_between(right.to_bytes(), target.to_bytes());
      return left_distance != right_distance ? left_distance < right_distance : left < right;
   });
   const auto first = table.closest(target.to_bytes(), 3);
   const auto second = table.closest(target.to_bytes(), 3);
   BOOST_REQUIRE_EQUAL(first.size(), 3U);
   BOOST_REQUIRE_EQUAL(second.size(), first.size());
   for (auto index = std::size_t{}; index < first.size(); ++index) {
      BOOST_TEST(first[index].id.to_string() == expected[index].to_string());
      BOOST_TEST(second[index].id.to_string() == first[index].id.to_string());
   }
}

BOOST_AUTO_TEST_CASE(p2p_dht_k_bucket_evicts_after_failure_threshold) {
   const auto local = peer(101);
   const auto peers = peers_in_same_dht_bucket(local, 2);
   auto table = dht::routing_table{
       local, custom_test_dht_profile(
                  dht::mode::client,
                  dht::options{.replication = 1, .alpha = 1, .replacement_cache_size = 1, .failure_threshold = 2})
                  .limits};
   table.upsert(dht::peer{.id = peers[0]});
   table.upsert(dht::peer{.id = peers[1]});

   table.mark_failure(peers[0]);
   BOOST_TEST(table.snapshot().front().id.to_string() == peers[0].to_string());
   table.mark_failure(peers[0]);

   const auto active = table.snapshot();
   BOOST_REQUIRE_EQUAL(active.size(), 1U);
   BOOST_TEST(active.front().id.to_string() == peers[1].to_string());
   BOOST_TEST(table.status().replacements == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_failed_target_seed_is_not_complete_or_closest) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto target = peer(19);
   auto target_endpoint = make_dns_tcp_endpoint(4019, "failed-target.example.com");
   target_endpoint.peer = target;

   const auto result = forge::asio::blocking::run(
       runtime,
       dht_query::run(
           dht_query::request{
               .target = make_dht_key(target),
               .target_peer = target,
               .options = custom_test_dht_profile(dht::mode::client, dht::options{.replication = 2, .alpha = 1}).limits,
               .seeds = std::vector<dht::peer>{dht::peer{
                   .id = target,
                   .endpoints = std::vector<endpoint>{target_endpoint},
                   .connection = dht::connection_type::can_connect,
               }},
           },
           [](const dht::peer&) -> boost::asio::awaitable<dht::message> {
              FORGE_THROW_CODE(forge::net::p2p::exceptions::code::timeout, "failed DHT target seed");
              co_return dht::message{};
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }));

   BOOST_TEST(!result.query.complete);
   BOOST_TEST(result.query.closest_peers.empty());
   BOOST_REQUIRE_EQUAL(result.failed.size(), 1U);
   BOOST_TEST(result.failed.front().to_string() == target.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_stops_after_closest_nonfailed_k_peers_are_queried) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto target = make_dht_key(peer(29));
   auto candidates = std::vector<dht::peer>{};
   for (const auto value : {30U, 31U, 32U}) {
      const auto id = peer(static_cast<std::uint8_t>(value));
      auto candidate_endpoint =
          make_dns_tcp_endpoint(static_cast<std::uint16_t>(4'000U + value), "query-convergence.example.com");
      candidate_endpoint.peer = id;
      candidates.push_back(dht::peer{
          .id = id,
          .endpoints = std::vector<endpoint>{std::move(candidate_endpoint)},
          .connection = dht::connection_type::can_connect,
      });
   }
   std::ranges::sort(candidates, [&](const auto& left, const auto& right) {
      const auto left_distance = distance_between(left.id.to_bytes(), target.bytes);
      const auto right_distance = distance_between(right.id.to_bytes(), target.bytes);
      return left_distance != right_distance ? left_distance < right_distance : left.id < right.id;
   });
   auto queried = std::vector<peer_id>{};

   const auto result = forge::asio::blocking::run(
       runtime,
       dht_query::run(
           dht_query::request{
               .target = target,
               .options = custom_test_dht_profile(dht::mode::client, dht::options{.replication = 2, .alpha = 1}).limits,
               .seeds = candidates,
           },
           [&queried](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
              queried.push_back(candidate.id);
              co_return dht::message{.type = dht::message_type::find_node};
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }));

   BOOST_REQUIRE_EQUAL(queried.size(), 2U);
   BOOST_TEST(queried[0].to_string() == candidates[0].id.to_string());
   BOOST_TEST(queried[1].to_string() == candidates[1].id.to_string());
   BOOST_REQUIRE_EQUAL(result.query.closest_peers.size(), 2U);
   BOOST_TEST(result.query.closest_peers[0].id.to_string() == candidates[0].id.to_string());
   BOOST_TEST(result.query.closest_peers[1].id.to_string() == candidates[1].id.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_dht_fanout_early_quorum_cancels_and_drains_blocked_sibling) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   using signal_channel = boost::asio::experimental::concurrent_channel<void(boost::system::error_code)>;
   auto slow_started = std::make_shared<signal_channel>(runtime.context().get_executor(), 1);
   auto slow_wait = std::make_shared<boost::asio::steady_timer>(runtime.context(), std::chrono::hours{1});
   const auto slow = peer(59);
   const auto fast = peer(60);
   const auto unnecessary = peer(61);
   auto slow_drained = false;
   auto unnecessary_started = false;

   auto pending = boost::asio::co_spawn(
       runtime.context(),
       detail::dht_fanout::run(runtime.context(),
                               detail::dht_fanout::request{
                                   .peers = {slow, fast, unnecessary},
                                   .concurrency = 2,
                                   .success_target = 1,
                                   .timeout = std::chrono::seconds{1},
                                   .operation = "test DHT fanout",
                               },
                               [slow_started, slow_wait, slow, fast, unnecessary, &slow_drained, &unnecessary_started](
                                   const peer_id& current, std::chrono::milliseconds) -> boost::asio::awaitable<bool> {
                                  if (current == slow) {
                                     static_cast<void>(slow_started->try_send(boost::system::error_code{}));
                                     auto error = boost::system::error_code{};
                                     co_await slow_wait->async_wait(
                                         boost::asio::redirect_error(boost::asio::use_awaitable, error));
                                     slow_drained = true;
                                     co_return false;
                                  }
                                  if (current == fast) {
                                     co_await slow_started->async_receive(boost::asio::use_awaitable);
                                     co_return true;
                                  }
                                  if (current == unnecessary) {
                                     unnecessary_started = true;
                                     co_return true;
                                  }
                                  throw std::runtime_error{"unexpected DHT fanout peer"};
                               }),
       boost::asio::use_future);

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();
   BOOST_TEST(result.succeeded == 1U);
   BOOST_TEST(result.attempted == 2U);
   BOOST_TEST(slow_drained);
   BOOST_TEST(!unnecessary_started);
}

BOOST_AUTO_TEST_CASE(p2p_dht_fanout_full_target_attempts_every_closest_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto first = peer(62);
   const auto second = peer(63);
   auto attempted = std::vector<peer_id>{};

   const auto result = forge::asio::blocking::run(
       runtime, detail::dht_fanout::run(
                    runtime.context(),
                    detail::dht_fanout::request{
                        .peers = {first, second},
                        .concurrency = 1,
                        .success_target = 2,
                        .timeout = std::chrono::seconds{1},
                        .operation = "test full DHT fanout",
                    },
                    [&attempted](const peer_id& current, std::chrono::milliseconds) -> boost::asio::awaitable<bool> {
                       attempted.push_back(current);
                       co_return true;
                    }));

   BOOST_TEST(result.succeeded == 2U);
   BOOST_TEST(result.attempted == 2U);
   BOOST_REQUIRE_EQUAL(attempted.size(), 2U);
   BOOST_TEST(attempted[0].to_string() == first.to_string());
   BOOST_TEST(attempted[1].to_string() == second.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_fast_completion_advances_while_sibling_is_blocked) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   using signal_channel = boost::asio::experimental::concurrent_channel<void(boost::system::error_code)>;
   auto slow_started = std::make_shared<signal_channel>(runtime.context().get_executor(), 1);
   auto slow_wait = std::make_shared<boost::asio::steady_timer>(runtime.context(), std::chrono::hours{1});
   const auto slow = peer(45);
   const auto fast = peer(46);
   const auto replacement = peer(47);
   const auto provider = peer(48);
   const auto candidate = [](peer_id id, std::uint16_t port, std::string host) {
      auto endpoint = make_dns_tcp_endpoint(port, std::move(host));
      endpoint.peer = id;
      return dht::peer{.id = std::move(id),
                       .endpoints = std::vector<forge::net::p2p::endpoint>{std::move(endpoint)},
                       .connection = dht::connection_type::can_connect};
   };
   const auto slow_candidate = candidate(slow, 4'045, "query-event-slow.example.com");
   const auto fast_candidate = candidate(fast, 4'046, "query-event-fast.example.com");
   const auto replacement_candidate = candidate(replacement, 4'047, "query-event-next.example.com");
   auto slow_drained = false;
   auto advanced_before_slow_drain = false;

   auto pending = boost::asio::co_spawn(
       runtime.context(),
       dht_query::run(
           dht_query::request{
               .target = make_dht_key(peer(49)),
               .options = custom_test_dht_profile(dht::mode::client,
                                                  dht::options{.replication = 3, .alpha = 2, .max_query_peers = 4})
                              .limits,
               .seeds = std::vector<dht::peer>{slow_candidate, fast_candidate},
               .requested_provider_count = 1,
           },
           [slow_started, slow_wait, slow, fast, replacement, provider, replacement_candidate, &slow_drained,
            &advanced_before_slow_drain](const dht::peer& current) -> boost::asio::awaitable<dht::message> {
              if (current.id == slow) {
                 static_cast<void>(slow_started->try_send(boost::system::error_code{}));
                 auto error = boost::system::error_code{};
                 co_await slow_wait->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
                 slow_drained = true;
                 co_return dht::message{.type = dht::message_type::find_node};
              }
              if (current.id == fast) {
                 co_await slow_started->async_receive(boost::asio::use_awaitable);
                 co_return dht::message{.type = dht::message_type::find_node,
                                        .closer_peers = std::vector<dht::peer>{replacement_candidate}};
              }
              if (current.id == replacement) {
                 advanced_before_slow_drain = !slow_drained;
                 co_return dht::message{
                     .type = dht::message_type::get_providers,
                     .provider_peers = std::vector<dht::peer>{dht::peer{.id = provider}},
                 };
              }
              throw std::runtime_error{"unexpected DHT query candidate"};
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }),
       boost::asio::use_future);

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();
   BOOST_TEST(advanced_before_slow_drain);
   BOOST_TEST(slow_drained);
   BOOST_REQUIRE_EQUAL(result.query.provider_peers.size(), 1U);
   BOOST_TEST(result.query.provider_peers.front().id.to_string() == provider.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_early_target_success_cancels_and_drains_children) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   using signal_channel = boost::asio::experimental::concurrent_channel<void(boost::system::error_code)>;
   auto slow_started = std::make_shared<signal_channel>(runtime.context().get_executor(), 1);
   auto slow_wait = std::make_shared<boost::asio::steady_timer>(runtime.context(), std::chrono::hours{1});
   const auto target = peer(50);
   const auto slow = peer(51);
   auto target_endpoint = make_dns_tcp_endpoint(4'050, "query-target-success.example.com");
   target_endpoint.peer = target;
   auto slow_endpoint = make_dns_tcp_endpoint(4'051, "query-target-slow.example.com");
   slow_endpoint.peer = slow;
   auto slow_drained = false;

   auto pending = boost::asio::co_spawn(
       runtime.context(),
       dht_query::run(
           dht_query::request{
               .target = make_dht_key(target),
               .target_peer = target,
               .options = custom_test_dht_profile(dht::mode::client, dht::options{.replication = 2, .alpha = 2}).limits,
               .seeds =
                   std::vector<dht::peer>{
                       dht::peer{.id = target,
                                 .endpoints = std::vector<endpoint>{std::move(target_endpoint)},
                                 .connection = dht::connection_type::can_connect},
                       dht::peer{.id = slow,
                                 .endpoints = std::vector<endpoint>{std::move(slow_endpoint)},
                                 .connection = dht::connection_type::can_connect},
                   },
           },
           [slow_started, slow_wait, target, slow,
            &slow_drained](const dht::peer& current) -> boost::asio::awaitable<dht::message> {
              if (current.id == slow) {
                 static_cast<void>(slow_started->try_send(boost::system::error_code{}));
                 auto error = boost::system::error_code{};
                 co_await slow_wait->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
                 slow_drained = true;
                 co_return dht::message{.type = dht::message_type::find_node};
              }
              if (current.id == target) {
                 co_await slow_started->async_receive(boost::asio::use_awaitable);
                 co_return dht::message{.type = dht::message_type::find_node};
              }
              throw std::runtime_error{"unexpected DHT query candidate"};
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }),
       boost::asio::use_future);

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();
   BOOST_TEST(result.query.complete);
   BOOST_TEST(slow_drained);
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_value_quorum_cancels_and_drains_children) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   using signal_channel = boost::asio::experimental::concurrent_channel<void(boost::system::error_code)>;
   auto slow_started = std::make_shared<signal_channel>(runtime.context().get_executor(), 1);
   auto slow_wait = std::make_shared<boost::asio::steady_timer>(runtime.context(), std::chrono::hours{1});
   const auto slow = peer(62);
   const auto fast = peer(63);
   const auto unnecessary = peer(64);
   const auto overflow = peer(65);
   const auto candidate = [](peer_id id, std::uint16_t port) {
      auto address = make_dns_tcp_endpoint(port, "value-quorum.example.com");
      address.peer = id;
      return dht::peer{
          .id = std::move(id), .endpoints = {std::move(address)}, .connection = dht::connection_type::can_connect};
   };
   auto slow_drained = false;
   auto unnecessary_started = false;
   auto accepted = std::size_t{};
   const auto key = make_dht_key(std::vector<std::uint8_t>{'q', 'u', 'o', 'r', 'u', 'm'});

   auto pending = boost::asio::co_spawn(
       runtime.context(),
       dht_query::run(
           dht_query::request{
               .target = key,
               .options = custom_test_dht_profile(dht::mode::client,
                                                  dht::options{.replication = 3, .alpha = 2, .max_query_peers = 3})
                              .limits,
               .seeds = {candidate(slow, 4'062), candidate(fast, 4'063), candidate(unnecessary, 4'064)},
               .collect_value_responses = true,
           },
           [slow_started, slow_wait, slow, fast, unnecessary, overflow, key, candidate, &slow_drained,
            &unnecessary_started](const dht::peer& current) -> boost::asio::awaitable<dht::message> {
              if (current.id == slow) {
                 static_cast<void>(slow_started->try_send(boost::system::error_code{}));
                 auto error = boost::system::error_code{};
                 co_await slow_wait->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
                 slow_drained = true;
                 co_return dht::message{.type = dht::message_type::get_value};
              }
              if (current.id == fast) {
                 co_await slow_started->async_receive(boost::asio::use_awaitable);
                 co_return dht::message{
                     .type = dht::message_type::get_value,
                     .record_value = dht::record{.key_value = key, .value = {'v'}},
                     .closer_peers = {candidate(overflow, 4'065)},
                 };
              }
              if (current.id == unnecessary) {
                 unnecessary_started = true;
                 co_return dht::message{.type = dht::message_type::get_value};
              }
              throw std::runtime_error{"unexpected DHT value quorum candidate"};
           },
           [&accepted](const dht::peer&, dht::message& response) -> boost::asio::awaitable<bool> {
              if (response.record_value) {
                 ++accepted;
              }
              co_return accepted >= 1;
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }),
       boost::asio::use_future);

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();
   BOOST_TEST(accepted == 1U);
   BOOST_TEST(slow_drained);
   BOOST_TEST(!unnecessary_started);
   BOOST_REQUIRE_EQUAL(result.value_responses.size(), 1U);
   BOOST_REQUIRE(result.value_responses.front().second.has_value());
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_discovered_peer_bound_keeps_closest_candidates) {
   BOOST_TEST(amino_v1().limits.max_query_peers == 256U);
   auto invalid_limits = dht::options{};
   invalid_limits.max_query_peers = 0;
   BOOST_CHECK_THROW(static_cast<void>(custom_test_dht_profile(dht::mode::client, invalid_limits)),
                     exceptions::invalid_options);

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto seed = peer(52);
   auto seed_endpoint = make_dns_tcp_endpoint(4'052, "query-bound-seed.example.com");
   seed_endpoint.peer = seed;
   const auto discovered = [](std::uint8_t value, std::uint16_t port) {
      const auto id = peer(value);
      auto endpoint = make_dns_tcp_endpoint(port, "query-bound-discovered.example.com");
      endpoint.peer = id;
      return dht::peer{.id = id,
                       .endpoints = std::vector<forge::net::p2p::endpoint>{std::move(endpoint)},
                       .connection = dht::connection_type::can_connect};
   };
   auto attempts = std::size_t{};
   auto pending = boost::asio::co_spawn(
       runtime.context(),
       dht_query::run(
           dht_query::request{
               .target = make_dht_key(peer(55)),
               .options = custom_test_dht_profile(dht::mode::client,
                                                  dht::options{.replication = 2, .alpha = 1, .max_query_peers = 2})
                              .limits,
               .seeds = std::vector<dht::peer>{dht::peer{
                   .id = seed,
                   .endpoints = std::vector<endpoint>{std::move(seed_endpoint)},
                   .connection = dht::connection_type::can_connect,
               }},
           },
           [&attempts, discovered](const dht::peer&) -> boost::asio::awaitable<dht::message> {
              ++attempts;
              co_return dht::message{
                  .type = dht::message_type::find_node,
                  .closer_peers = std::vector<dht::peer>{discovered(53, 4'053), discovered(54, 4'054)},
              };
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }),
       boost::asio::use_future);

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();
   BOOST_TEST(attempts >= 2U);
   BOOST_TEST(attempts <= 3U);
   BOOST_TEST(result.query.closest_peers.size() == 2U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_failure_is_not_also_reported_as_queried) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto successful = peer(56);
   const auto failed = peer(57);
   const auto candidate = [](peer_id id, std::uint16_t port) {
      auto endpoint = make_dns_tcp_endpoint(port, "query-accounting.example.com");
      endpoint.peer = id;
      return dht::peer{.id = std::move(id),
                       .endpoints = std::vector<forge::net::p2p::endpoint>{std::move(endpoint)},
                       .connection = dht::connection_type::can_connect};
   };
   auto attempts = std::map<peer_id, std::size_t>{};
   auto pending = boost::asio::co_spawn(
       runtime.context(),
       dht_query::run(
           dht_query::request{
               .target = make_dht_key(peer(58)),
               .options = custom_test_dht_profile(dht::mode::client, dht::options{.replication = 2, .alpha = 2}).limits,
               .seeds = std::vector<dht::peer>{candidate(successful, 4'056), candidate(failed, 4'057)},
           },
           [&attempts, failed](const dht::peer& current) -> boost::asio::awaitable<dht::message> {
              ++attempts[current.id];
              if (current.id == failed) {
                 FORGE_THROW_CODE(exceptions::code::timeout, "injected DHT peer failure");
              }
              co_return dht::message{.type = dht::message_type::find_node};
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }),
       boost::asio::use_future);

   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();
   BOOST_REQUIRE_EQUAL(result.queried.size(), 1U);
   BOOST_REQUIRE_EQUAL(result.failed.size(), 1U);
   BOOST_TEST(result.queried.front().to_string() == successful.to_string());
   BOOST_TEST(result.failed.front().to_string() == failed.to_string());
   BOOST_TEST(!std::ranges::contains(result.queried, failed));
   BOOST_TEST(attempts[successful] == 1U);
   BOOST_TEST(attempts[failed] == 1U);
}

BOOST_AUTO_TEST_CASE(p2p_dht_query_cancels_and_joins_children_with_parent) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto blocker = std::make_shared<boost::asio::steady_timer>(runtime.context(), std::chrono::hours{1});
   auto callable_lifetime = std::make_shared<int>(42);
   auto weak_lifetime = std::weak_ptr<int>{callable_lifetime};
   auto entered = std::promise<void>{};
   auto entered_future = entered.get_future();
   auto entered_count = std::size_t{};
   auto cancellation = boost::asio::cancellation_signal{};
   auto first_endpoint = make_dns_tcp_endpoint(4'042, "query-lifetime-a.example.com");
   first_endpoint.peer = peer(42);
   auto second_endpoint = make_dns_tcp_endpoint(4'043, "query-lifetime-b.example.com");
   second_endpoint.peer = peer(44);

   auto result = boost::asio::co_spawn(
       runtime.context(),
       dht_query::run(
           dht_query::request{
               .target = make_dht_key(peer(43)),
               .options = custom_test_dht_profile(dht::mode::client, dht::options{.replication = 2, .alpha = 2}).limits,
               .seeds =
                   std::vector<dht::peer>{
                       dht::peer{
                           .id = peer(42),
                           .endpoints = std::vector<endpoint>{std::move(first_endpoint)},
                           .connection = dht::connection_type::can_connect,
                       },
                       dht::peer{
                           .id = peer(44),
                           .endpoints = std::vector<endpoint>{std::move(second_endpoint)},
                           .connection = dht::connection_type::can_connect,
                       },
                   },
           },
           [blocker, owner = callable_lifetime, &entered,
            &entered_count](const dht::peer&) -> boost::asio::awaitable<dht::message> {
              if (++entered_count == 2) {
                 entered.set_value();
              }
              auto error = boost::system::error_code{};
              co_await blocker->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
              co_return dht::message{.type = dht::message_type::find_node};
           },
           [](const dht::peer&, const forge::exceptions::base&) { return true; }),
       boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   callable_lifetime.reset();
   BOOST_REQUIRE(entered_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);

   cancellation.emit(boost::asio::cancellation_type::terminal);
   BOOST_REQUIRE(result.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   BOOST_CHECK_THROW(static_cast<void>(result.get()), boost::system::system_error);
   BOOST_TEST(weak_lifetime.expired());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_codec_roundtrips_register_discover_cookie_and_status) {
   const auto opts = rendezvous::options{};
   const auto identity = make_test_identity();
   const auto legacy_payload_type =
       std::vector<std::uint8_t>{'/', 'l', 'i', 'b', 'p', '2', 'p', '/', 'r', 'o', 'u', 't', 'i', 'n',
                                 'g', '-', 's', 't', 'a', 't', 'e', '-', 'r', 'e', 'c', 'o', 'r', 'd'};
   BOOST_TEST(rendezvous::codec::peer_record_payload_type() == legacy_payload_type, boost::test_tools::per_element());
   const auto record = make_signed_rendezvous_peer_record(identity, {}, 42);
   const auto encoded_register = rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::register_peer,
       .register_value =
           rendezvous::register_request{
               .namespace_name = "forge.discovery",
               .signed_peer_record = record,
               .ttl = std::chrono::seconds{7'200},
           },
   });
   const auto decoded_register = rendezvous::codec::decode(encoded_register, opts);
   BOOST_TEST(static_cast<int>(decoded_register.type) == static_cast<int>(rendezvous::message_type::register_peer));
   BOOST_REQUIRE(decoded_register.register_value.has_value());
   BOOST_TEST(decoded_register.register_value->namespace_name == "forge.discovery");
   BOOST_TEST(decoded_register.register_value->signed_peer_record == record, boost::test_tools::per_element());
   BOOST_TEST(decoded_register.register_value->ttl == std::chrono::seconds{7'200});

   const auto decoded_peer_record = rendezvous::codec::open_peer_record(signed_envelope::decode(record), identity.peer);
   BOOST_TEST(decoded_peer_record.peer.to_string() == identity.peer.to_string());
   BOOST_REQUIRE_EQUAL(decoded_peer_record.endpoints.size(), 1U);
   BOOST_TEST(decoded_peer_record.sequence == 42U);

   const auto other_identity = make_test_identity();
   const auto mismatched_record = rendezvous::codec::seal_peer_record(
       rendezvous::peer_record{
           .peer = other_identity.peer,
           .endpoints = {},
           .sequence = 43,
       },
       identity.key, forge::crypto::pki::pem::read_private_key(identity.private_key_pem));
   BOOST_CHECK_THROW((void)rendezvous::codec::open_peer_record(mismatched_record), exceptions::invalid_identity);

   const auto cookie = rendezvous::codec::make_cookie(42, "forge.discovery");
   BOOST_TEST(rendezvous::codec::read_cookie(cookie) == 42U);
   BOOST_TEST(rendezvous::codec::read_cookie_namespace(cookie) == "forge.discovery");
   const auto encoded_discover = rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::discover_response,
       .discover_response_value =
           rendezvous::discover_response{
               .registrations = std::vector<rendezvous::registration>{rendezvous::registration{
                   .namespace_name = "forge.discovery",
                   .signed_peer_record = record,
                   .ttl = std::chrono::seconds{7'200},
               }},
               .cookie = cookie,
               .status_value = rendezvous::status::ok,
           },
   });
   const auto decoded_discover = rendezvous::codec::decode(encoded_discover, opts);
   BOOST_TEST(static_cast<int>(decoded_discover.type) == static_cast<int>(rendezvous::message_type::discover_response));
   BOOST_REQUIRE(decoded_discover.discover_response_value.has_value());
   BOOST_REQUIRE_EQUAL(decoded_discover.discover_response_value->registrations.size(), 1U);
   BOOST_TEST(decoded_discover.discover_response_value->registrations.front().namespace_name == "forge.discovery");
   BOOST_TEST(decoded_discover.discover_response_value->registrations.front().peer.to_string() ==
              identity.peer.to_string());
   BOOST_TEST(decoded_discover.discover_response_value->registrations.front().signed_peer_record == record,
              boost::test_tools::per_element());
   BOOST_TEST(rendezvous::codec::read_cookie(decoded_discover.discover_response_value->cookie) == 42U);

   BOOST_CHECK_THROW((void)rendezvous::codec::read_cookie(std::vector<std::uint8_t>{1, 2, 3}), forge::exceptions::base);
   BOOST_CHECK_THROW((void)rendezvous::codec::encode(rendezvous::message{
                         .type = rendezvous::message_type::discover,
                         .discover_value = rendezvous::discover_request{.namespace_name = std::string(300, 'x')},
                     }),
                     forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_peer_record_uses_canonical_libp2p_domain_type_and_wire_bytes) {
   const auto expected_payload_type = std::vector<std::uint8_t>{0x03, 0x01};

   const auto fixture = rendezvous::codec::encode_peer_record(rendezvous::peer_record{
       .peer = peer(42),
       .endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/tcp/4001")},
       .sequence = 42,
   });
   const auto canonical_go_rust_payload = std::vector<std::uint8_t>{
       0x0a, 0x03, 0x00, 0x01, 0x2a, 0x10, 0x2a, 0x1a, 0x0a, 0x0a, 0x08, 0x04, 0x7f, 0x00, 0x00, 0x01, 0x06, 0x0f, 0xa1,
   };
   BOOST_TEST(fixture == canonical_go_rust_payload, boost::test_tools::per_element());

   const auto identity = make_test_identity();
   const auto envelope = signed_envelope::decode(make_signed_identify_peer_record(identity));
   BOOST_TEST(envelope.payload_type == expected_payload_type, boost::test_tools::per_element());
   BOOST_CHECK_NO_THROW(envelope.verify("libp2p-peer-record", identity.peer));
   BOOST_CHECK_THROW(envelope.verify("libp2p-routing-state", identity.peer), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_codec_roundtrips_v11_rpc_and_rejects_malformed) {
   const auto identity = make_test_identity();
   auto message = pubsub::message{
       .from = identity.peer,
       .data = std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 7},
       .subject = pubsub::topic{.value = "forge.topic"},
       .key = encode_public_key(identity.key),
   };
   pubsub::codec::sign_message(message, forge::crypto::pki::pem::read_private_key(identity.private_key_pem));
   BOOST_TEST(pubsub::codec::verify_message(message));

   const auto id = pubsub::codec::message_id(message);
   BOOST_TEST(!id.empty());

   const auto encoded =
       pubsub::codec::encode(
           pubsub::rpc{
               .subscriptions =
                   std::vector<pubsub::subscription>{
                       pubsub::subscription{.subscribe = true, .subject = pubsub::topic{.value = "forge.topic"}},
                       pubsub::subscription{.subscribe = false, .subject = pubsub::topic{.value = "forge.old"}},
                   },
               .messages = std::vector<pubsub::message>{message},
               .control_value =
                   pubsub::control{
                       .have = std::vector<pubsub::control::ihave>{pubsub::control::ihave{
                           .subject = pubsub::topic{.value = "forge.topic"},
                           .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                       .want = std::vector<pubsub::control::iwant>{pubsub::control::iwant{
                           .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                       .grafts = std::vector<pubsub::control::graft>{pubsub::control::graft{
                           .subject = pubsub::topic{.value = "forge.topic"}}},
                       .prunes = std::vector<pubsub::control::prune>{pubsub::control::prune{
                           .subject = pubsub::topic{.value = "forge.topic"},
                           .peers = std::vector<pubsub::peer_info>{pubsub::peer_info{.peer = identity.peer}},
                           .backoff = std::chrono::seconds{60},
                       }},
                   },
           });

   const auto decoded = pubsub::codec::decode(encoded, pubsub::options{});
   BOOST_REQUIRE_EQUAL(decoded.subscriptions.size(), 2U);
   BOOST_TEST(decoded.subscriptions.front().subscribe);
   BOOST_TEST(decoded.subscriptions.front().subject.value == "forge.topic");
   BOOST_REQUIRE_EQUAL(decoded.messages.size(), 1U);
   BOOST_TEST(decoded.messages.front().data == message.data, boost::test_tools::per_element());
   BOOST_TEST(pubsub::codec::verify_message(decoded.messages.front()));
   BOOST_REQUIRE(decoded.control_value.has_value());
   BOOST_REQUIRE_EQUAL(decoded.control_value->have.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.control_value->want.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.control_value->grafts.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.control_value->prunes.size(), 1U);
   BOOST_TEST(decoded.control_value->prunes.front().backoff == std::chrono::seconds{60});

   BOOST_CHECK_THROW((void)pubsub::codec::decode(std::vector<std::uint8_t>{0xff, 0xff, 0xff, 0xff}, pubsub::options{}),
                     forge::exceptions::base);
   auto strict = pubsub::options{};
   strict.limits.max_rpc_size = 4;
   BOOST_CHECK_THROW((void)pubsub::codec::decode(encoded, strict), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_signing_rejects_tampered_payload) {
   const auto identity = make_test_identity();
   auto message = pubsub::message{
       .from = identity.peer,
       .data = std::vector<std::uint8_t>{'s', 'i', 'g', 'n', 'e', 'd'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 1},
       .subject = pubsub::topic{.value = "forge.signed"},
       .key = encode_public_key(identity.key),
   };
   pubsub::codec::sign_message(message, forge::crypto::pki::pem::read_private_key(identity.private_key_pem));
   BOOST_TEST(pubsub::codec::verify_message(message));

   message.data.push_back('!');
   BOOST_TEST(!pubsub::codec::verify_message(message));
}

BOOST_AUTO_TEST_CASE(p2p_signed_envelope_seals_and_verifies_domain_payload_and_signer) {
   const auto identity = make_test_identity();
   const auto payload_type = forge::multiformats::varint_encode(0x0302);
   const auto payload = std::vector<std::uint8_t>{1, 2, 3, 4, 5};

   const auto envelope =
       signed_envelope::seal(identity.key, forge::crypto::pki::pem::read_private_key(identity.private_key_pem),
                             "libp2p-relay-rsvp", payload_type, payload);
   const auto encoded = envelope.encode();
   const auto decoded = signed_envelope::decode(encoded);

   BOOST_TEST(decoded.payload_type == payload_type, boost::test_tools::per_element());
   BOOST_TEST(decoded.payload == payload, boost::test_tools::per_element());
   BOOST_TEST(decoded.signer().to_string() == identity.peer.to_string());
   BOOST_CHECK_NO_THROW(decoded.verify("libp2p-relay-rsvp", identity.peer));
   BOOST_CHECK_THROW(decoded.verify("wrong-domain", identity.peer), forge::exceptions::base);

   auto tampered = decoded;
   tampered.payload.back() ^= 0x01U;
   BOOST_CHECK_THROW(tampered.verify("libp2p-relay-rsvp", identity.peer), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_relay_voucher_uses_signed_envelope_and_rejects_stale_or_wrong_signer) {
   const auto relay_identity = make_test_identity();
   const auto other_identity = make_test_identity();
   const auto reservation = relay::voucher{
       .relay_peer = relay_identity.peer,
       .peer = peer(44),
       .expires_at = 4'102'444'800ULL,
   };

   const auto envelope = relay::codec::seal_reservation_voucher(
       reservation, relay_identity.key, forge::crypto::pki::pem::read_private_key(relay_identity.private_key_pem));
   const auto decoded = relay::codec::open_reservation_voucher(envelope, relay_identity.peer, 4'102'444'799ULL);

   BOOST_TEST(decoded.relay_peer.to_string() == reservation.relay_peer.to_string());
   BOOST_TEST(decoded.peer.to_string() == reservation.peer.to_string());
   BOOST_TEST(decoded.expires_at == reservation.expires_at);

   BOOST_CHECK_THROW(
       (void)relay::codec::open_reservation_voucher(envelope, relay_identity.peer, reservation.expires_at),
       forge::exceptions::base);
   BOOST_CHECK_THROW((void)relay::codec::open_reservation_voucher(envelope, other_identity.peer, 4'102'444'799ULL),
                     forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_resource_manager_enforces_relay_stream_and_queue_limits) {
   auto manager = resource_manager{resource_manager::limits{
       .max_streams = 2,
       .max_relay_streams = 1,
       .max_queued_bytes = 8,
   }};
   auto relay_stream = manager.reserve_relay_stream();
   BOOST_REQUIRE(relay_stream);
   BOOST_TEST(!manager.reserve_relay_stream());
   auto queued = manager.reserve_queued_bytes(8);
   BOOST_REQUIRE(queued);
   BOOST_TEST(!manager.reserve_queued_bytes(1));
   auto snapshot = manager.current();
   BOOST_TEST(snapshot.active_streams == 1U);
   BOOST_TEST(snapshot.active_relay_streams == 1U);
   BOOST_TEST(snapshot.queued_bytes == 8U);
   BOOST_TEST(snapshot.denied == 2U);
   BOOST_TEST(snapshot.denied_streams == 1U);
   BOOST_TEST(snapshot.denied_queued_bytes == 1U);
   BOOST_TEST(snapshot.denied_relays == 0U);
   relay_stream.reset();
   queued.reset();
   snapshot = manager.current();
   BOOST_TEST(snapshot.active_streams == 0U);
   BOOST_TEST(snapshot.active_relay_streams == 0U);
   BOOST_TEST(snapshot.queued_bytes == 0U);

   auto saturated = resource_manager{resource_manager::limits{
       .max_streams = 1,
       .max_relay_streams = 4,
   }};
   auto regular_stream = saturated.reserve_stream();
   BOOST_REQUIRE(regular_stream);
   auto before = saturated.current();
   BOOST_TEST(!saturated.reserve_relay_stream());
   auto after = saturated.current();
   BOOST_TEST(after.denied == before.denied + 1U);
   BOOST_TEST(after.active_streams == 1U);
   BOOST_TEST(after.active_relay_streams == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_relay_byte_limits_are_independent_per_circuit_direction) {
   auto left_to_right = detail::relay_budget{4};
   auto right_to_left = detail::relay_budget{4};

   BOOST_TEST(left_to_right.consume(4));
   BOOST_TEST(right_to_left.consume(4));
   BOOST_TEST(left_to_right.used() == 4U);
   BOOST_TEST(right_to_left.used() == 4U);
   BOOST_TEST(left_to_right.exhausted());
   BOOST_TEST(right_to_left.exhausted());
   BOOST_TEST(!left_to_right.consume(1));
   BOOST_TEST(!right_to_left.consume(1));

   auto next_circuit = detail::relay_budget{4};
   BOOST_TEST(next_circuit.consume(4));
}

BOOST_AUTO_TEST_CASE(p2p_relay_deadline_cancel_before_wait_is_latched) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto manager = resource_manager{};
   auto reservation = manager.reserve_relay_stream();
   BOOST_REQUIRE(reservation);
   auto left_backend = std::make_shared<queued_transport_stream>(501);
   auto right_backend = std::make_shared<queued_transport_stream>(502);
   auto pair = std::make_shared<detail::relay_pair>(
       peer(50), forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(left_backend)},
       forge::net::p2p::stream{forge::net::transport::detail::stream_access::make(right_backend)},
       std::move(*reservation), runtime.context().get_executor(), std::chrono::seconds{60}, 1024);

   BOOST_TEST(!pair->mark_finished());
   BOOST_TEST(pair->mark_finished());
   auto deadline = boost::asio::co_spawn(runtime.context(), pair->async_wait_deadline(), boost::asio::use_future);
   BOOST_REQUIRE(deadline.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_TEST(!deadline.get());
   BOOST_TEST(manager.current().active_relay_streams == 1U);
   pair.reset();
   BOOST_TEST(manager.current().active_relay_streams == 0U);
}

static_assert(requires { diagnostics::connection_state{std::size_t{1}, std::vector<peer_id>{peer(51)}}; });

BOOST_AUTO_TEST_CASE(p2p_resource_manager_enforces_peer_protocol_dial_and_reservation_scopes) {
   auto manager = resource_manager{resource_manager::limits{
       .max_streams = 4,
       .max_streams_per_peer = 1,
       .max_streams_per_protocol = 1,
       .max_relay_reservations = 1,
       .max_dial_attempts_per_peer = 1,
       .max_malformed_messages_per_peer = 1,
   }};
   const auto scope = resource_manager::scope{.peer = peer(93), .protocol = builtins::relay_hop};
   const auto other = resource_manager::scope{.peer = peer(94), .protocol = builtins::relay_hop};

   auto scoped = manager.reserve_stream(scope);
   BOOST_REQUIRE(scoped);
   BOOST_TEST(!manager.reserve_stream(scope));
   scoped.reset();
   scoped = manager.reserve_stream(scope);
   BOOST_REQUIRE(scoped);
   scoped.reset();

   auto other_stream = manager.reserve_stream(other);
   BOOST_REQUIRE(other_stream);
   BOOST_TEST(!manager.reserve_stream(scope));
   other_stream.reset();
   auto scoped_snapshot = manager.current();
   BOOST_TEST(scoped_snapshot.active_streams == 0U);
   BOOST_TEST(scoped_snapshot.active_peer_scopes == 0U);
   BOOST_TEST(scoped_snapshot.active_protocol_scopes == 0U);

   auto saturated = resource_manager{resource_manager::limits{
       .max_streams = 1,
       .max_streams_per_peer = 4,
       .max_streams_per_protocol = 4,
   }};
   auto saturated_stream =
       saturated.reserve_stream(resource_manager::scope{.peer = peer(96), .protocol = builtins::relay_hop});
   BOOST_REQUIRE(saturated_stream);
   BOOST_TEST(!saturated.reserve_stream(resource_manager::scope{.peer = peer(97), .protocol = builtins::ping}));
   auto saturated_snapshot = saturated.current();
   BOOST_TEST(saturated_snapshot.active_streams == 1U);
   BOOST_TEST(saturated_snapshot.active_peer_scopes == 1U);
   BOOST_TEST(saturated_snapshot.active_protocol_scopes == 1U);

   auto relay = manager.reserve_relay(scope);
   BOOST_REQUIRE(relay);
   BOOST_TEST(!manager.reserve_relay(resource_manager::scope{.peer = peer(95), .protocol = builtins::relay_hop}));
   relay.reset();

   auto dial = manager.reserve_dial();
   BOOST_REQUIRE(dial);
   BOOST_TEST(dial->bind(scope.peer));
   BOOST_TEST(!manager.reserve_dial(scope.peer));
   BOOST_TEST(manager.current().active_dials == 1U);
   dial.reset();
   BOOST_TEST(manager.current().active_dials == 0U);
   BOOST_TEST(manager.record_malformed(scope));
   BOOST_TEST(!manager.record_malformed(scope));

   const auto snapshot = manager.current();
   BOOST_TEST(snapshot.denied >= 4U);
   BOOST_TEST(snapshot.denied_streams >= 2U);
   BOOST_TEST(snapshot.denied_dials >= 1U);
   BOOST_TEST(snapshot.denied_relays >= 1U);
   BOOST_TEST(snapshot.denied_malformed >= 1U);
}

BOOST_AUTO_TEST_CASE(p2p_resource_manager_enforces_connection_session_scopes) {
   auto manager = resource_manager{resource_manager::limits{
       .max_pending_inbound_sessions = 1,
       .max_pending_outbound_sessions = 1,
       .max_inbound_sessions = 1,
       .max_outbound_sessions = 1,
       .max_sessions_per_peer = 1,
   }};
   const auto inbound = resource_manager::session_scope{
       .peer = peer(231),
       .direction = resource_manager::session_direction::inbound,
   };
   const auto outbound = resource_manager::session_scope{
       .peer = peer(232),
       .direction = resource_manager::session_direction::outbound,
   };

   auto pending_inbound = manager.reserve_session(resource_manager::session_direction::inbound);
   BOOST_REQUIRE(pending_inbound);
   BOOST_TEST(!manager.reserve_session(resource_manager::session_direction::inbound));
   pending_inbound.reset();
   auto pending_outbound = manager.reserve_session(resource_manager::session_direction::outbound);
   BOOST_REQUIRE(pending_outbound);
   BOOST_TEST(!manager.reserve_session(resource_manager::session_direction::outbound));
   pending_outbound.reset();

   auto active_inbound = manager.reserve_session(resource_manager::session_direction::inbound);
   BOOST_REQUIRE(active_inbound);
   BOOST_TEST(active_inbound->establish(inbound));
   auto duplicate = manager.reserve_session(resource_manager::session_direction::inbound);
   BOOST_REQUIRE(duplicate);
   BOOST_TEST(!duplicate->establish(inbound));
   duplicate.reset();
   auto active_outbound = manager.reserve_session(resource_manager::session_direction::outbound);
   BOOST_REQUIRE(active_outbound);
   BOOST_TEST(active_outbound->establish(outbound));
   active_inbound.reset();
   active_outbound.reset();

   const auto snapshot = manager.current();
   BOOST_TEST(snapshot.active_inbound_sessions == 0U);
   BOOST_TEST(snapshot.active_outbound_sessions == 0U);
   BOOST_TEST(snapshot.pending_inbound_sessions == 0U);
   BOOST_TEST(snapshot.pending_outbound_sessions == 0U);
   BOOST_TEST(snapshot.denied >= 3U);
   BOOST_TEST(snapshot.denied_sessions >= 3U);

   auto saturated = resource_manager{resource_manager::limits{
       .max_inbound_sessions = 1,
       .max_outbound_sessions = 0,
       .max_sessions_per_peer = 1,
   }};
   auto saturated_session = saturated.reserve_session(resource_manager::session_direction::inbound);
   BOOST_REQUIRE(saturated_session);
   BOOST_TEST(saturated_session->establish(
       resource_manager::session_scope{.peer = peer(233), .direction = resource_manager::session_direction::inbound}));
   auto rejected_session = saturated.reserve_session(resource_manager::session_direction::inbound);
   BOOST_REQUIRE(rejected_session);
   BOOST_TEST(!rejected_session->establish(
       resource_manager::session_scope{.peer = peer(234), .direction = resource_manager::session_direction::inbound}));
   auto saturated_snapshot = saturated.current();
   BOOST_TEST(saturated_snapshot.active_session_peer_scopes == 1U);
   rejected_session.reset();
   saturated_session.reset();
   saturated_snapshot = saturated.current();
   BOOST_TEST(saturated_snapshot.active_session_peer_scopes == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_node_peer_protection_api_is_tagged_and_additive) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(233))};
   const auto protected_peer = peer(234);

   BOOST_TEST(!value.is_peer_protected(protected_peer));
   value.protect_peer(protected_peer, "bootstrap");
   BOOST_TEST(value.is_peer_protected(protected_peer));
   BOOST_TEST(value.unprotect_peer(protected_peer, "other"));
   BOOST_TEST(value.is_peer_protected(protected_peer));
   BOOST_TEST(!value.unprotect_peer(protected_peer, "bootstrap"));
   BOOST_TEST(!value.is_peer_protected(protected_peer));

   forge::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_snapshot_defaults_persistence_state) {
   auto value = diagnostics::snapshot{};

   BOOST_TEST(!value.persistence.degraded);
   BOOST_TEST(value.persistence.pending_peer_mutations == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_snapshot_reports_live_network_state_without_mutation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server = node{
       runtime, options_for(peer(250), capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                                              capabilities::pubsub})};
   auto client_options =
       options_for(peer(251), capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                                     capabilities::pubsub | capabilities::relay_reservation});
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto client = node{runtime, std::move(client_options)};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)listen(client, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                                        capabilities::pubsub | capabilities::relay |
                                                        capabilities::relay_reservation});
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   client.protect_peer(server.local_peer(), "diagnostics-test");
   client.peers().mark_endpoint_failure(server.local_peer(), server_endpoint, path::kind::direct,
                                        std::chrono::system_clock::now() + std::chrono::seconds{30});

   const auto before = client.metrics();
   const auto before_pubsub = client.pubsub_snapshot();
   const auto snapshot = client.diagnostics(diagnostics::options{});
   const auto after = client.metrics();

   BOOST_TEST(snapshot.network.local_peer.to_string() == client.local_peer().to_string());
   BOOST_TEST(!snapshot.network.local_endpoints.empty());
   BOOST_TEST(snapshot.metrics.active_sessions == before.active_sessions);
   BOOST_TEST(snapshot.metrics.active_sessions == after.active_sessions);
   BOOST_TEST(snapshot.resources.active_outbound_sessions >= 1U);
   BOOST_TEST(snapshot.pubsub.topics == before_pubsub.topics);
   BOOST_REQUIRE_EQUAL(snapshot.peers.size(), 1U);
   BOOST_TEST(snapshot.peers.front().peer.to_string() == server.local_peer().to_string());
   BOOST_TEST(snapshot.peers.front().protected_peer);
   BOOST_REQUIRE(!snapshot.peers.front().endpoints.empty());
   BOOST_TEST(snapshot.peers.front().endpoints.front().failures >= 1U);
   BOOST_REQUIRE_EQUAL(snapshot.sessions.size(), 1U);
   BOOST_TEST(snapshot.sessions.front().remote_peer.to_string() == server.local_peer().to_string());
   BOOST_TEST(snapshot.sessions.front().protected_peer);
   BOOST_TEST(static_cast<int>(snapshot.sessions.front().direction) ==
              static_cast<int>(diagnostics::session_direction::outbound));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_snapshot_caps_are_deterministic) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(252))};
   value.peers().learn_endpoint(peer(253), make_quic_endpoint(4001), capability_set{.bits = capabilities::direct_quic});
   value.peers().learn_endpoint(peer(254), make_tcp_endpoint(4002),
                                capability_set{.bits = capabilities::peer_exchange});

   const auto snapshot = value.diagnostics(diagnostics::options{
       .max_peers = 1,
       .max_sessions = 0,
       .max_endpoints_per_peer = 1,
       .max_protocols_per_peer = 1,
       .max_relay_reservations_per_peer = 1,
   });

   BOOST_REQUIRE_EQUAL(snapshot.peers.size(), 1U);
   BOOST_TEST(snapshot.sessions.empty());
   BOOST_REQUIRE_EQUAL(snapshot.peers.front().endpoints.size(), 1U);

   forge::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_prunes_unprotected_sessions_and_keeps_protected_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(235));
   client_options.limits.max_sessions = 2;
   client_options.limits.max_outbound_sessions = 2;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(236))};
   auto second = node{runtime, options_for(peer(237))};
   auto third = node{runtime, options_for(peer(238))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);
   register_echo(second);
   register_echo(third);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   const auto third_endpoint = listen(third, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   client.protect_peer(first.local_peer(), "bootstrap");
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));

   auto metrics = client.metrics();
   BOOST_TEST(metrics.active_sessions == 2U);
   BOOST_TEST(metrics.sessions_pruned >= 1U);
   const auto diagnostics = client.diagnostics();
   BOOST_TEST(diagnostics.connections.retained_identify_attempts <= diagnostics.connections.active_sessions);
   BOOST_TEST(client.is_peer_protected(first.local_peer()));

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(first.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'p', 'r', 'o', 't', 'e', 'c', 't'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, third.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_prunes_batch_to_low_watermark) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   auto client_options = options_for(peer(60));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 4;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{60'000};

   auto first = node{runtime, options_for(peer(61))};
   auto second = node{runtime, options_for(peer(62))};
   auto third = node{runtime, options_for(peer(63))};
   auto fourth = node{runtime, options_for(peer(64))};
   auto fifth = node{runtime, options_for(peer(65))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(fifth);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   const auto third_endpoint = listen(third, runtime);
   const auto fourth_endpoint = listen(fourth, runtime);
   const auto fifth_endpoint = listen(fifth, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(fourth_endpoint, node::connect_options{.expected_peer = fourth.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(fifth_endpoint, node::connect_options{.expected_peer = fifth.local_peer()}));

   auto metrics = client.metrics();
   BOOST_TEST(metrics.active_sessions == 2U);
   BOOST_TEST(metrics.sessions_pruned >= 3U);
   const auto diagnostics = client.diagnostics();
   BOOST_TEST(diagnostics.connections.retained_identify_attempts <= diagnostics.connections.active_sessions);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(fifth.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'b', 'a', 't', 'c', 'h'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, fifth.async_stop());
   forge::asio::blocking::run(runtime, fourth.async_stop());
   forge::asio::blocking::run(runtime, third.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_rejects_when_all_sessions_are_protected) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(239));
   client_options.limits.max_sessions = 2;
   client_options.limits.max_outbound_sessions = 2;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(240))};
   auto second = node{runtime, options_for(peer(241))};
   auto third = node{runtime, options_for(peer(242))};
   auto client = node{runtime, std::move(client_options)};

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   const auto third_endpoint = listen(third, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   client.protect_peer(first.local_peer(), "bootstrap");
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
   client.protect_peer(second.local_peer(), "bootstrap");

   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));
      BOOST_FAIL("expected all-protected session limit rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   const auto metrics = client.metrics();
   BOOST_TEST(metrics.active_sessions == 2U);
   BOOST_TEST(metrics.connection_rejections >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, third.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_rejects_direction_limit_before_pruning_unrelated_sessions) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(254));
   client_options.limits.max_sessions = 2;
   client_options.limits.max_inbound_sessions = 4;
   client_options.limits.max_outbound_sessions = 1;
   client_options.limits.max_sessions_per_peer = 4;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(255))};
   auto second = node{runtime, options_for(peer(1))};
   auto third = node{runtime, options_for(peer(2))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);
   register_echo(second);
   register_echo(client);

   const auto first_endpoint = listen(first, runtime);
   (void)listen(second, runtime);
   const auto client_endpoint = listen(client, runtime);
   const auto third_endpoint = listen(third, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   client.protect_peer(first.local_peer(), "bootstrap");
   (void)forge::asio::blocking::run(
       runtime, second.async_connect(client_endpoint, node::connect_options{.expected_peer = client.local_peer()}));
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "inbound session admission");
   const auto before = client.metrics();
   BOOST_TEST(before.active_sessions == 2U);

   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));
      BOOST_FAIL("expected outbound session limit rejection");
   } catch (const forge::exceptions::base&) {
   }

   const auto after = client.metrics();
   BOOST_TEST(after.active_sessions == 2U);
   BOOST_TEST(after.sessions_pruned == before.sessions_pruned);
   BOOST_TEST(after.connection_rejections >= before.connection_rejections + 1U);

   auto stream =
       forge::asio::blocking::run(runtime, second.async_open_protocol_stream(client.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'s', 't', 'a', 'y'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, third.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_prunes_direction_saturated_stale_session_before_global_cap) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(3));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_inbound_sessions = 4;
   client_options.limits.max_outbound_sessions = 1;
   client_options.limits.max_sessions_per_peer = 4;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(4))};
   auto second = node{runtime, options_for(peer(5))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);
   register_echo(second);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   const auto before = client.metrics();
   BOOST_TEST(before.active_sessions == 1U);

   (void)forge::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));

   const auto after = client.metrics();
   BOOST_TEST(after.active_sessions == 1U);
   BOOST_TEST(after.sessions_pruned >= before.sessions_pruned + 1U);
   BOOST_TEST(after.connection_rejections == before.connection_rejections);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(second.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'d', 'i', 'r'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_prunes_direction_saturated_session_below_low_watermark) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(6));
   client_options.limits.max_sessions = 100;
   client_options.limits.max_inbound_sessions = 100;
   client_options.limits.max_outbound_sessions = 1;
   client_options.limits.max_sessions_per_peer = 4;
   client_options.limits.session_low_watermark = 50;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(7))};
   auto second = node{runtime, options_for(peer(8))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);
   register_echo(second);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   const auto before = client.metrics();
   BOOST_TEST(before.active_sessions == 1U);

   (void)forge::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));

   const auto after = client.metrics();
   BOOST_TEST(after.active_sessions == 1U);
   BOOST_TEST(after.sessions_pruned >= before.sessions_pruned + 1U);
   BOOST_TEST(after.connection_rejections == before.connection_rejections);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(second.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'l', 'o', 'w'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_enforces_outbound_session_limit) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(243));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 1;
   client_options.limits.session_low_watermark = 4;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};
   auto first = node{runtime, options_for(peer(244))};
   auto second = node{runtime, options_for(peer(245))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   client.protect_peer(first.local_peer(), "bootstrap");
   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
      BOOST_FAIL("expected outbound session limit rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(first.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'l', 'i', 'm', 'i', 't'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().connection_rejections >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_allows_bounded_parallel_sessions_per_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(250));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 4;
   client_options.limits.max_sessions_per_peer = 2;
   client_options.limits.session_low_watermark = 4;
   auto server = node{runtime, options_for(peer(251))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(server);

   const auto endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(client.metrics().active_sessions == 2U);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                                             node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'p', 'a', 'r', 'a', 'l', 'l', 'e', 'l'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_enforces_sessions_per_peer_limit) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(252));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 4;
   client_options.limits.max_sessions_per_peer = 1;
   client_options.limits.session_low_watermark = 4;
   auto server = node{runtime, options_for(peer(253))};
   auto client = node{runtime, std::move(client_options)};

   const auto endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
      BOOST_FAIL("expected per-peer session limit rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   BOOST_TEST(client.metrics().active_sessions == 1U);
   BOOST_TEST(client.metrics().connection_rejections >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_rejects_pending_outbound_limit_without_killing_first_attempt) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client_options = options_for(peer(246));
   client_options.limits.max_pending_outbound_sessions = 1;
   auto client = node{runtime, std::move(client_options)};
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime, std::chrono::milliseconds{500});

   auto first = boost::asio::co_spawn(runtime.context(),
                                      client.async_connect(stalled_endpoint,
                                                           node::connect_options{
                                                               .expected_peer = peer(247),
                                                               .allow_relay = false,
                                                               .timeout = std::chrono::milliseconds{500},
                                                           }),
                                      boost::asio::use_future);
   wait_on_runtime(runtime, std::chrono::milliseconds{50}, "pending outbound admission");

   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_connect(stalled_endpoint, node::connect_options{
                                                              .expected_peer = peer(247),
                                                              .allow_relay = false,
                                                              .timeout = std::chrono::milliseconds{100},
                                                          }));
      BOOST_FAIL("expected pending outbound session limit rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }

   try {
      (void)first.get();
   } catch (const forge::exceptions::base&) {
   }
   BOOST_TEST(client.metrics().connection_rejections >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_stop_cancels_pending_direct_connect) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(peer(248))};
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime, std::chrono::seconds{5});

   auto pending = boost::asio::co_spawn(runtime.context(),
                                        client.async_connect(stalled_endpoint,
                                                             node::connect_options{
                                                                 .expected_peer = peer(249),
                                                                 .allow_relay = false,
                                                                 .timeout = std::chrono::seconds{5},
                                                             }),
                                        boost::asio::use_future);
   wait_on_runtime(runtime, std::chrono::milliseconds{50}, "pending direct connect");

   const auto started = std::chrono::steady_clock::now();
   forge::asio::blocking::run(runtime, client.async_stop());
   const auto elapsed = std::chrono::steady_clock::now() - started;

   BOOST_TEST(elapsed < std::chrono::milliseconds{750});
   BOOST_REQUIRE(pending.wait_for(std::chrono::milliseconds{750}) == std::future_status::ready);
   try {
      (void)pending.get();
      BOOST_FAIL("expected pending direct connect cancellation");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::canceled));
   }
}

BOOST_AUTO_TEST_CASE(p2p_stop_rejects_session_completing_during_shutdown) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};

   for (auto remaining = 32U; remaining != 0U; --remaining) {
      auto server = node{runtime, options_for(peer(250))};
      auto client = node{runtime, options_for(peer(251))};
      const auto endpoint = listen(server, runtime);

      auto pending = boost::asio::co_spawn(runtime.context(),
                                           client.async_connect(endpoint,
                                                                node::connect_options{
                                                                    .expected_peer = server.local_peer(),
                                                                    .allow_relay = false,
                                                                    .timeout = std::chrono::seconds{2},
                                                                }),
                                           boost::asio::use_future);

      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
      while (server.metrics().active_sessions == 0U &&
             pending.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
         BOOST_REQUIRE(std::chrono::steady_clock::now() < deadline);
         wait_on_runtime(runtime, std::chrono::milliseconds{1}, "outbound shutdown race");
      }

      forge::asio::blocking::run(runtime, client.async_stop());
      BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
      try {
         (void)pending.get();
      } catch (const forge::exceptions::base& error) {
         BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
         const auto code = *forge::net::p2p::exceptions::code_of(error);
         const auto canceled_or_closed = code == exceptions::code::canceled || code == exceptions::code::closed;
         BOOST_TEST(canceled_or_closed);
      }

      const auto snapshot = client.diagnostics();
      BOOST_TEST(snapshot.metrics.active_sessions == 0U);
      BOOST_TEST(snapshot.sessions.empty());
      BOOST_TEST(snapshot.network.stopped);

      forge::asio::blocking::run(runtime, server.async_stop());
   }
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_relay_hop_codec_matches_spec_shape) {
   auto reserve = relay::hop_message{.kind = relay::hop_message::message_kind::reserve};
   BOOST_TEST(relay::codec::encode_hop(reserve) == std::vector<std::uint8_t>({0x02, 0x08, 0x00}),
              boost::test_tools::per_element());

   auto status = relay::hop_message{
       .kind = relay::hop_message::message_kind::status,
       .status = relay::status::ok,
   };
   BOOST_TEST(relay::codec::encode_hop(status) == std::vector<std::uint8_t>({0x04, 0x08, 0x02, 0x28, 0x64}),
              boost::test_tools::per_element());
   auto decoded = relay::codec::decode_hop(relay::codec::encode_hop(status));
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(relay::hop_message::message_kind::status));
   BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(relay::status::ok));
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_relay_wire_roundtrips_statuses_limits_and_voucher) {
   const auto identity = make_test_identity();
   const auto relay_peer = identity.peer;
   const auto target_peer = peer(97);
   const auto relay_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4103/quic-v1/p2p/" + relay_peer.to_string());
   const auto voucher = relay::codec::seal_reservation_voucher(
       relay::voucher{
           .relay_peer = relay_peer,
           .peer = target_peer,
           .expires_at = 1'777'000'000,
       },
       identity.key, forge::crypto::pki::pem::read_private_key(identity.private_key_pem));

   auto decoded_voucher = relay::codec::open_reservation_voucher(voucher, relay_peer, 1'776'999'999);
   BOOST_TEST(decoded_voucher.relay_peer.to_string() == relay_peer.to_string());
   BOOST_TEST(decoded_voucher.peer.to_string() == target_peer.to_string());
   BOOST_TEST(decoded_voucher.expires_at == 1'777'000'000ULL);

   for (auto status :
        {relay::status::ok, relay::status::reservation_refused, relay::status::resource_limit_exceeded,
         relay::status::permission_denied, relay::status::connection_failed, relay::status::no_reservation,
         relay::status::malformed_message, relay::status::unexpected_message}) {
      auto decoded = relay::codec::decode_hop(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .reservation_value =
              relay::reservation{
                  .expires_at = 1'777'000'000,
                  .relay_endpoints = std::vector<endpoint>{relay_endpoint},
                  .voucher = voucher,
              },
          .limit_value = relay::limit{.duration = std::chrono::seconds{60}, .data = 4096},
          .status = status,
      }));
      BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(relay::hop_message::message_kind::status));
      BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(status));
      BOOST_REQUIRE(decoded.limit_value.has_value());
      BOOST_TEST(decoded.limit_value->duration == std::chrono::seconds{60});
      BOOST_TEST(decoded.limit_value->data == 4096U);
      if (status == relay::status::ok) {
         BOOST_REQUIRE(decoded.reservation_value.has_value());
         BOOST_REQUIRE(decoded.reservation_value->voucher.has_value());
         BOOST_TEST(decoded.reservation_value->voucher->encode() == voucher.encode(), boost::test_tools::per_element());
      }
   }

   BOOST_CHECK_THROW((void)relay::codec::decode_hop(std::vector<std::uint8_t>{0x02, 0x28, 0x64}),
                     forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_dcutr_codec_matches_spec_shape) {
   auto connect = hole_punch::message{.kind = hole_punch::message::message_kind::connect};
   BOOST_TEST(hole_punch::codec::encode(connect) == std::vector<std::uint8_t>({0x02, 0x08, 0x64}),
              boost::test_tools::per_element());

   auto decoded = hole_punch::codec::decode(hole_punch::codec::encode(hole_punch::message{
       .kind = hole_punch::message::message_kind::sync,
   }));
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(hole_punch::message::message_kind::sync));
}

BOOST_AUTO_TEST_CASE(p2p_dcutr_attempt_tracks_rtt_retry_and_inflight_state) {
   auto attempt = hole_punch::attempt{};
   attempt.peer = peer(98);
   attempt.relay_peer = peer(99);
   attempt.rtt = std::chrono::milliseconds{80};
   attempt.max_attempts = 2;
   BOOST_TEST(attempt.sync_delay() == std::chrono::milliseconds{40});
   BOOST_TEST(attempt.try_begin());
   BOOST_TEST(!attempt.try_begin());
   attempt.finish(hole_punch::status::failed);
   BOOST_TEST(attempt.can_retry());
   BOOST_TEST(attempt.try_begin());
   attempt.finish(hole_punch::status::succeeded);
   BOOST_TEST(!attempt.can_retry());
   BOOST_TEST(static_cast<int>(attempt.result().value) == static_cast<int>(hole_punch::status::succeeded));
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_autonat_v1_codec_matches_spec_shape) {
   const auto id = peer(91);
   const auto endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4101/quic-v1/p2p/" + id.to_string());
   auto dial = reachability::message{
       .kind = reachability::message::message_kind::dial,
       .peer =
           reachability::peer_info{
               .peer = id,
               .endpoints = std::vector<forge::net::p2p::endpoint>{endpoint},
           },
   };

   auto decoded = reachability::codec::decode_v1(reachability::codec::encode_v1(dial));
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(reachability::message::message_kind::dial));
   BOOST_REQUIRE(decoded.peer.has_value());
   BOOST_TEST(decoded.peer->peer.to_string() == id.to_string());
   BOOST_REQUIRE_EQUAL(decoded.peer->endpoints.size(), 1U);
   BOOST_TEST(decoded.peer->endpoints.front().to_string() == endpoint.to_string());

   auto response = reachability::codec::decode_v1(reachability::codec::encode_v1(reachability::message{
       .kind = reachability::message::message_kind::dial_response,
       .response =
           reachability::dial_response{
               .status = reachability::dial_status::ok,
               .endpoint = endpoint,
           },
   }));
   BOOST_REQUIRE(response.response.has_value());
   BOOST_TEST(static_cast<int>(response.response->status) == static_cast<int>(reachability::dial_status::ok));
   BOOST_REQUIRE(response.response->endpoint.has_value());
   BOOST_TEST(response.response->endpoint->to_string() == endpoint.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_autonat_v2_codec_covers_nonce_data_and_statuses) {
   const auto id = peer(92);
   const auto remote_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4102/quic-v1/p2p/" + id.to_string());
   auto request = reachability::v2::message{
       .type = reachability::v2::message::kind::dial_request,
       .dial_request =
           reachability::v2::dial_request{
               .endpoints = std::vector<forge::net::p2p::endpoint>{remote_endpoint},
               .nonce = 0x0102'0304'0506'0708ULL,
           },
   };

   auto decoded = reachability::codec::decode_v2(reachability::codec::encode_v2(request));
   BOOST_TEST(static_cast<int>(decoded.type) == static_cast<int>(reachability::v2::message::kind::dial_request));
   BOOST_REQUIRE(decoded.dial_request.has_value());
   BOOST_TEST(decoded.dial_request->nonce == 0x0102'0304'0506'0708ULL);
   BOOST_REQUIRE_EQUAL(decoded.dial_request->endpoints.size(), 1U);
   BOOST_TEST(decoded.dial_request->endpoints.front().to_string() == remote_endpoint.to_string());

   auto data_request = reachability::codec::decode_v2(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_data_request,
       .dial_data_request = reachability::v2::dial_data_request{.index = 1, .bytes = 30 * 1024},
   }));
   BOOST_REQUIRE(data_request.dial_data_request.has_value());
   BOOST_TEST(data_request.dial_data_request->index == 1U);
   BOOST_TEST(data_request.dial_data_request->bytes == 30U * 1024U);

   auto data_response = reachability::codec::decode_v2(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_data_response,
       .dial_data_response = reachability::v2::dial_data_response{.data = std::vector<std::uint8_t>(4096, 0x5a)},
   }));
   BOOST_REQUIRE(data_response.dial_data_response.has_value());
   BOOST_TEST(data_response.dial_data_response->data.size() == 4096U);

   auto response = reachability::codec::decode_v2(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_response,
       .dial_response =
           reachability::v2::dial_response{
               .status = reachability::v2::response_status::ok,
               .index = 1,
               .dial_status = reachability::v2::dial_status::dial_back_error,
           },
   }));
   BOOST_REQUIRE(response.dial_response.has_value());
   BOOST_TEST(static_cast<int>(response.dial_response->status) ==
              static_cast<int>(reachability::v2::response_status::ok));
   BOOST_TEST(response.dial_response->index == 1U);
   BOOST_TEST(static_cast<int>(response.dial_response->dial_status) ==
              static_cast<int>(reachability::v2::dial_status::dial_back_error));

   auto dial_back = reachability::codec::decode_v2_dial_back(
       reachability::codec::encode_v2_dial_back(reachability::v2::dial_back{.nonce = request.dial_request->nonce}));
   BOOST_TEST(dial_back.nonce == request.dial_request->nonce);

   auto dial_back_response =
       reachability::codec::decode_v2_dial_back_response(reachability::codec::encode_v2_dial_back_response(
           reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
   BOOST_TEST(static_cast<int>(dial_back_response.status) == static_cast<int>(reachability::v2::dial_back_status::ok));
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_autonat_v2_rejects_oversized_data_response_and_unknown_status) {
   BOOST_CHECK_THROW(
       (void)reachability::codec::encode_v2(reachability::v2::message{
           .type = reachability::v2::message::kind::dial_data_response,
           .dial_data_response = reachability::v2::dial_data_response{.data = std::vector<std::uint8_t>(4097, 0x42)},
       }),
       forge::exceptions::base);

   // Message { dialResponse: DialResponse { status: 999 } }
   BOOST_CHECK_THROW(
       (void)reachability::codec::decode_v2(std::vector<std::uint8_t>{0x06, 0x12, 0x04, 0x08, 0xe7, 0x07}),
       forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_autonat_v2_probe_public_and_persists_observation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto observer =
       node{runtime, options_for(peer(100), capability_set{.bits = capabilities::direct_quic | capabilities::autonat})};
   auto subject =
       node{runtime, options_for(peer(101), capability_set{.bits = capabilities::direct_quic | capabilities::autonat})};

   const auto observer_endpoint = listen(observer, runtime);
   (void)listen(subject, runtime);
   subject.peers().learn_endpoint(observer.local_peer(), observer_endpoint,
                                  capability_set{.bits = capabilities::direct_quic | capabilities::autonat});

   const auto state = forge::asio::blocking::run(runtime, subject.async_probe_reachability(observer.local_peer()));
   BOOST_TEST(static_cast<int>(state) == static_cast<int>(reachability::state::publicly_reachable));

   const auto stored = subject.peers().find(subject.local_peer());
   BOOST_REQUIRE(stored.has_value());
   BOOST_TEST(static_cast<int>(stored->reachability) == static_cast<int>(reachability::state::publicly_reachable));
   BOOST_REQUIRE(stored->observed_endpoint.has_value());

   forge::asio::blocking::run(runtime, subject.async_stop());
   forge::asio::blocking::run(runtime, observer.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_reservation_persists_candidate) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto relay_node =
       node{runtime, options_for(peer(102), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client = node{runtime, options_for(peer(103), capability_set{.bits = capabilities::direct_quic |
                                                                             capabilities::relay_reservation})};

   const auto relay_endpoint = listen(relay_node, runtime);
   client.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   const auto info = forge::asio::blocking::run(runtime, client.async_reserve_relay(relay_node.local_peer()));
   BOOST_TEST(info.relay_peer.to_string() == relay_node.local_peer().to_string());
   BOOST_TEST(!info.voucher.has_value());

   const auto stored = client.peers().find(relay_node.local_peer());
   BOOST_REQUIRE(stored.has_value());
   BOOST_REQUIRE_EQUAL(stored->relay_reservations.size(), 1U);
   BOOST_TEST(stored->relay_reservations.front().relay.to_string() == relay_node.local_peer().to_string());
   BOOST_TEST(stored->relay_reservations.front().voucher.empty());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_reserves_peer_store_candidate) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto relay_node =
       node{runtime, options_for(peer(104), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(105), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 2;
   client_options.relay_policy.max_parallel_reservations = 1;
   auto client = node{runtime, std::move(client_options)};

   const auto relay_endpoint = listen(relay_node, runtime);
   client.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   const auto reservations = forge::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(reservations.front().relay_peer.to_string() == relay_node.local_peer().to_string());

   const auto stored = client.peers().find(relay_node.local_peer());
   BOOST_REQUIRE(stored.has_value());
   BOOST_REQUIRE_EQUAL(stored->relay_reservations.size(), 1U);
   BOOST_TEST(stored->relay_reservations.front().relay.to_string() == relay_node.local_peer().to_string());
   BOOST_TEST(client.metrics().relay_discovery_refreshes == 1U);
   BOOST_TEST(client.metrics().relay_discovery_attempts == 1U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_backs_off_failed_candidate_and_tries_next) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto bad_options = options_for(peer(106), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                    capabilities::relay_reservation});
   bad_options.relay_policy.service_enabled = false;
   auto bad_relay = node{runtime, std::move(bad_options)};
   auto good_relay =
       node{runtime, options_for(peer(107), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(108), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 2;
   client_options.relay_policy.max_parallel_reservations = 1;
   client_options.relay_policy.candidate_backoff = std::chrono::seconds{30};
   auto client = node{runtime, std::move(client_options)};

   const auto bad_endpoint = listen(bad_relay, runtime);
   const auto good_endpoint = listen(good_relay, runtime);
   client.peers().learn_endpoint(
       bad_relay.local_peer(), bad_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   client.peers().learn_endpoint(
       good_relay.local_peer(), good_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   auto bad_record = client.peers().find(bad_relay.local_peer()).value();
   bad_record.score = 100.0;
   client.peers().upsert(std::move(bad_record));

   const auto reservations = forge::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(reservations.front().relay_peer.to_string() == good_relay.local_peer().to_string());
   BOOST_TEST(client.metrics().relay_discovery_attempts == 2U);
   BOOST_TEST(client.metrics().relay_discovery_failures == 1U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 1U);

   const auto failed = client.peers().find(bad_relay.local_peer());
   BOOST_REQUIRE(failed.has_value());
   BOOST_TEST(failed->discovery_backoff_until > std::chrono::system_clock::now());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, good_relay.async_stop());
   forge::asio::blocking::run(runtime, bad_relay.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_accepts_dht_and_rendezvous_sourced_candidates) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto dht_relay =
       node{runtime, options_for(peer(116), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto rendezvous_relay =
       node{runtime, options_for(peer(117), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(120), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 2;
   client_options.relay_policy.max_candidates_per_refresh = 2;
   client_options.relay_policy.max_parallel_reservations = 1;
   auto client = node{runtime, std::move(client_options)};

   const auto dht_endpoint = listen(dht_relay, runtime);
   const auto rendezvous_endpoint = listen(rendezvous_relay, runtime);
   client.peers().upsert(peer_store::record{
       .peer = dht_relay.local_peer(),
       .capabilities =
           capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation},
       .discovered_by = discovery::source::dht,
       .endpoints = std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{.endpoint = dht_endpoint}},
       .discovery_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5},
   });
   client.peers().upsert(peer_store::record{
       .peer = rendezvous_relay.local_peer(),
       .capabilities =
           capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation},
       .discovered_by = discovery::source::rendezvous,
       .endpoints =
           std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{.endpoint = rendezvous_endpoint}},
       .discovery_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5},
   });

   const auto reservations = forge::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 2U);
   BOOST_TEST(client.metrics().relay_discovery_attempts == 2U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 2U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, rendezvous_relay.async_stop());
   forge::asio::blocking::run(runtime, dht_relay.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_respects_candidate_and_target_limits) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto first =
       node{runtime, options_for(peer(113), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto second =
       node{runtime, options_for(peer(114), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(115), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 1;
   client_options.relay_policy.max_parallel_reservations = 1;
   auto client = node{runtime, std::move(client_options)};

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   client.peers().learn_endpoint(
       first.local_peer(), first_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   client.peers().learn_endpoint(
       second.local_peer(), second_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   const auto reservations = forge::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(client.metrics().relay_discovery_attempts == 1U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_fallback_refreshes_candidate_without_explicit_relay_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("autorelay-fallback-relay");
   const auto source_identity = make_test_certificate_identity("autorelay-fallback-source");
   const auto target_identity = make_test_certificate_identity("autorelay-fallback-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source_options = options_for(
       source_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   source_options.relay_policy.target_reservations = 1;
   source_options.relay_policy.max_candidates_per_refresh = 2;
   source_options.relay_policy.max_parallel_reservations = 1;
   auto source = node{runtime, std::move(source_options)};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   const auto closed_direct_endpoint = start_stalling_tcp_peer(runtime, std::chrono::milliseconds{10});
   source.peers().learn_endpoint(target.local_peer(), closed_direct_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   auto stream = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'a', 'u', 't', 'o'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(source.metrics().relay_discovery_refreshes >= 1U);
   BOOST_TEST(source.metrics().relay_discovery_successes >= 1U);
   BOOST_TEST(source.metrics().path_relay_opens >= 1U);

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_policy_options_are_behavioral) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto relay_options = options_for(peer(110), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                      capabilities::relay_reservation});
   relay_options.relay_policy.service_enabled = false;
   auto relay_node = node{runtime, std::move(relay_options)};
   auto client = node{runtime, options_for(peer(111), capability_set{.bits = capabilities::direct_quic |
                                                                             capabilities::relay_reservation})};

   const auto relay_endpoint = listen(relay_node, runtime);
   client.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   try {
      (void)forge::asio::blocking::run(runtime, client.async_reserve_relay(relay_node.local_peer()));
      BOOST_FAIL("expected relay service policy rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::relay_rejected));
   }

   auto disabled_client_options = options_for(peer(112), capability_set{.bits = capabilities::direct_quic});
   disabled_client_options.relay_policy.client_enabled = false;
   auto disabled_client = node{runtime, std::move(disabled_client_options)};
   try {
      (void)forge::asio::blocking::run(runtime, disabled_client.async_reserve_relay(relay_node.local_peer()));
      BOOST_FAIL("expected relay client policy rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::relay_not_available));
   }

   forge::asio::blocking::run(runtime, disabled_client.async_stop());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_duration_requires_exact_wire_seconds) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto relay_options = options_for(peer(109), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                      capabilities::relay_reservation});
   relay_options.limits.relay.max_duration = std::chrono::milliseconds{1'500};

   BOOST_CHECK_THROW((void)node(runtime, std::move(relay_options)), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_relay_connect_requires_target_reservation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-reservation-owner-relay");
   const auto source_identity = make_test_certificate_identity("relay-reservation-owner-source");
   const auto target_identity = make_test_certificate_identity("relay-reservation-owner-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   const auto target_endpoint = listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   relay_node.peers().learn_endpoint(target.local_peer(), target_endpoint,
                                     capability_set{.bits = capabilities::direct_quic});
   (void)forge::asio::blocking::run(runtime, source.async_reserve_relay(relay_node.local_peer()));

   try {
      (void)forge::asio::blocking::run(
          runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                     node::open_options{
                                                         .allow_relay = true,
                                                         .relay_peer = relay_node.local_peer(),
                                                         .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                         .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                         .allow_hole_punch = false,
                                                     }));
      BOOST_FAIL("expected relay CONNECT rejection without target reservation");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::relay_rejected));
   }

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_stop_connect_passes_frames_after_target_reservation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-stop-connect-relay");
   const auto source_identity = make_test_certificate_identity("relay-stop-connect-source");
   const auto target_identity = make_test_certificate_identity("relay-stop-connect-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   auto stream = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .relay_peer = relay_node.local_peer(),
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'r', 'e', 'l', 'a', 'y'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(relay_node.metrics().relays_opened >= 1U);
   BOOST_TEST(source.metrics().path_relay_opens >= 1U);

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_duration_closes_circuit_and_releases_resources) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-duration-relay");
   const auto source_identity = make_test_certificate_identity("relay-duration-source");
   const auto target_identity = make_test_certificate_identity("relay-duration-target");
   auto relay_options =
       options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                          capabilities::relay_reservation});
   relay_options.limits.relay.max_duration = std::chrono::seconds{1};
   auto relay_node = node{runtime, std::move(relay_options)};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   auto stream = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .relay_peer = relay_node.local_peer(),
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto opened_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (relay_node.metrics().active_relays == 0 && std::chrono::steady_clock::now() < opened_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relay duration admission");
   }
   BOOST_REQUIRE(relay_node.metrics().active_relays == 1U);
   const auto admitted_resources = relay_node.diagnostics().resources;
   BOOST_REQUIRE(admitted_resources.active_relay_streams > 0U);

   const auto closed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
   while ((relay_node.metrics().active_relays != 0 || relay_node.diagnostics().resources.active_relay_streams != 0) &&
          std::chrono::steady_clock::now() < closed_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relay duration cleanup");
   }
   BOOST_TEST(relay_node.metrics().active_relays == 0U);
   BOOST_TEST(relay_node.diagnostics().resources.active_relay_streams == 0U);

   auto read = boost::asio::co_spawn(runtime.context(), stream.async_read_frame(), boost::asio::use_future);
   if (read.wait_for(std::chrono::seconds{1}) != std::future_status::ready) {
      stream.cancel();
      BOOST_FAIL("relay duration did not close the client stream");
   } else {
      BOOST_CHECK_THROW((void)read.get(), forge::exceptions::base);
   }

   auto next = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .relay_peer = relay_node.local_peer(),
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'n', 'e', 'x', 't'};
   forge::asio::blocking::run(runtime, next.async_write_frame(payload));
   BOOST_TEST(forge::asio::blocking::run(runtime, next.async_read_frame()) == payload,
              boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_renewal_preserves_active_circuit_limit) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-renewal-relay");
   const auto first_identity = make_test_certificate_identity("relay-renewal-first");
   const auto second_identity = make_test_certificate_identity("relay-renewal-second");
   const auto target_identity = make_test_certificate_identity("relay-renewal-target");
   auto relay_options =
       options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                          capabilities::relay_reservation});
   relay_options.limits.relay.max_streams_per_reservation = 1;
   relay_options.limits.relay.max_duration = std::chrono::seconds{10};
   auto relay_node = node{runtime, std::move(relay_options)};
   auto first = node{runtime, options_for(first_identity, capability_set{.bits = capabilities::direct_quic})};
   auto second = node{runtime, options_for(second_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   const auto relay_capabilities =
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation};
   first.peers().learn_endpoint(relay_node.local_peer(), relay_endpoint, relay_capabilities);
   second.peers().learn_endpoint(relay_node.local_peer(), relay_endpoint, relay_capabilities);
   target.peers().learn_endpoint(relay_node.local_peer(), relay_endpoint, relay_capabilities);
   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   const auto open_options = node::open_options{
       .allow_relay = true,
       .relay_peer = relay_node.local_peer(),
       .direct_attempt_timeout = std::chrono::milliseconds{100},
       .relay_attempt_timeout = std::chrono::milliseconds{2'000},
       .allow_hole_punch = false,
   };
   auto first_stream = forge::asio::blocking::run(
       runtime, first.async_open_protocol_stream(target.local_peer(), builtins::echo, open_options));
   const auto admitted_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (relay_node.metrics().active_relays != 1U && std::chrono::steady_clock::now() < admitted_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relay renewal initial circuit");
   }
   BOOST_REQUIRE(relay_node.metrics().active_relays == 1U);

   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));
   BOOST_CHECK_THROW((void)forge::asio::blocking::run(
                         runtime, second.async_open_protocol_stream(target.local_peer(), builtins::echo, open_options)),
                     forge::exceptions::base);
   BOOST_TEST(relay_node.metrics().active_relays == 1U);

   first_stream.cancel();
   forge::asio::blocking::run(runtime, first.async_stop());
   const auto released_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (relay_node.metrics().active_relays != 0U && std::chrono::steady_clock::now() < released_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relay renewal circuit release");
   }
   BOOST_REQUIRE(relay_node.metrics().active_relays == 0U);

   auto second_stream = forge::asio::blocking::run(
       runtime, second.async_open_protocol_stream(target.local_peer(), builtins::echo, open_options));
   const auto payload = std::vector<std::uint8_t>{'r', 'e', 'n', 'e', 'w'};
   forge::asio::blocking::run(runtime, second_stream.async_write_frame(payload));
   BOOST_TEST(forge::asio::blocking::run(runtime, second_stream.async_read_frame()) == payload,
              boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_reservation_is_released_after_last_disconnect) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-disconnect-relay");
   const auto first_identity = make_test_certificate_identity("relay-disconnect-first");
   const auto second_identity = make_test_certificate_identity("relay-disconnect-second");
   auto relay_options =
       options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                          capabilities::relay_reservation});
   relay_options.limits.relay.max_reservations = 1;
   relay_options.limits.resources.max_relay_reservations = 1;
   auto relay_node = node{runtime, std::move(relay_options)};
   auto first = node{runtime, options_for(first_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                 capabilities::relay_reservation})};
   auto second = node{runtime, options_for(second_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};

   const auto relay_endpoint = listen(relay_node, runtime);
   const auto relay_capabilities =
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation};
   first.peers().learn_endpoint(relay_node.local_peer(), relay_endpoint, relay_capabilities);
   second.peers().learn_endpoint(relay_node.local_peer(), relay_endpoint, relay_capabilities);
   (void)forge::asio::blocking::run(runtime, first.async_reserve_relay(relay_node.local_peer()));
   BOOST_REQUIRE(relay_node.metrics().active_relay_reservations == 1U);

   forge::asio::blocking::run(runtime, first.async_stop());
   const auto released_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (relay_node.metrics().active_relay_reservations != 0U &&
          std::chrono::steady_clock::now() < released_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relay reservation disconnect release");
   }
   BOOST_REQUIRE(relay_node.metrics().active_relay_reservations == 0U);

   (void)forge::asio::blocking::run(runtime, second.async_reserve_relay(relay_node.local_peer()));
   BOOST_TEST(relay_node.metrics().active_relay_reservations == 1U);

   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_only_open_does_not_record_direct_failure_or_evict_dht_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-only-accounting-relay");
   const auto source_identity = make_test_certificate_identity("relay-only-accounting-source");
   const auto target_identity = make_test_certificate_identity("relay-only-accounting-target");
   auto relay_options =
       options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                          capabilities::relay_reservation});
   auto custom_limits = dht::options{};
   custom_limits.failure_threshold = 1;
   auto source_options = dht_options_for(source_identity, custom_test_dht_profile(dht::mode::client, custom_limits));
   source_options.path_policy.allow_direct = false;
   auto target_options =
       dht_options_for(target_identity, custom_test_dht_profile(dht::mode::server, custom_limits),
                       capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   auto relay_node = node{runtime, std::move(relay_options)};
   auto source = node{runtime, std::move(source_options)};
   auto target = node{runtime, std::move(target_options)};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   const auto source_endpoint = listen(source, runtime);
   const auto target_endpoint = listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)forge::asio::blocking::run(
       runtime, target.async_connect(source_endpoint, node::connect_options{.expected_peer = source.local_peer()}));
   for (auto attempt = 0U; attempt < 20U && source.routing_status(content_swarm_test_dht).active == 0U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "inbound DHT Identify admission");
   }
   BOOST_REQUIRE(source.routing_status(content_swarm_test_dht).active > 0U);
   (void)forge::asio::blocking::run(runtime, source.async_reserve_relay(relay_node.local_peer()));
   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   const auto routing_before = source.routing_status(content_swarm_test_dht);
   const auto metrics_before = source.metrics();
   const auto target_before = source.peers().find(target.local_peer());
   BOOST_REQUIRE(target_before.has_value());
   BOOST_REQUIRE(routing_before.active > 0U);

   auto stream = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .relay_peer = relay_node.local_peer(),
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'r', 'e', 'l', 'a', 'y', '-', 'o', 'n', 'l', 'y'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   const auto metrics_after = source.metrics();
   const auto target_after = source.peers().find(target.local_peer());
   BOOST_REQUIRE(target_after.has_value());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(metrics_after.path_direct_attempts == metrics_before.path_direct_attempts);
   BOOST_TEST(metrics_after.direct_failures == metrics_before.direct_failures);
   BOOST_TEST(target_after->failures == target_before->failures);
   BOOST_TEST(source.routing_status(content_swarm_test_dht).active == routing_before.active);

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_transport_opens_arbitrary_registered_protocol) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto protocol = protocol_id{.value = "/product/relay-arbitrary/1"};
   const auto relay_identity = make_test_certificate_identity("relay-arbitrary-protocol-relay");
   const auto source_identity = make_test_certificate_identity("relay-arbitrary-protocol-source");
   const auto target_identity = make_test_certificate_identity("relay-arbitrary-protocol-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target, protocol);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)forge::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   const auto open_options = node::open_options{
       .allow_relay = true,
       .relay_peer = relay_node.local_peer(),
       .direct_attempt_timeout = std::chrono::milliseconds{100},
       .relay_attempt_timeout = std::chrono::milliseconds{2'000},
       .allow_hole_punch = false,
   };
   auto stream = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), protocol, open_options));
   const auto payload = std::vector<std::uint8_t>{'p', 'r', 'o', 'd', 'u', 'c', 't'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   forge::asio::blocking::run(runtime, stream.async_close());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(relay_node.metrics().relays_opened >= 1U);
   BOOST_TEST(source.metrics().path_relay_opens >= 1U);

   const auto relay_session_count = [](const node& value, const peer_id& remote, const peer_id& relay,
                                       diagnostics::session_direction direction, bool require_identified) {
      return std::ranges::count_if(value.diagnostics().sessions, [&](const diagnostics::session& session) {
         return !session.closed && session.remote_peer == remote && session.path == path::kind::relay &&
                session.relay_peer == relay && session.direction == direction &&
                (!require_identified || session.identify_state == identify::state::identified);
      });
   };
   auto relay_sessions_identified = false;
   for (auto attempt = 0U; attempt < 200U && !relay_sessions_identified; ++attempt) {
      relay_sessions_identified = relay_session_count(source, target.local_peer(), relay_node.local_peer(),
                                                      diagnostics::session_direction::outbound, true) == 1 &&
                                  relay_session_count(target, source.local_peer(), relay_node.local_peer(),
                                                      diagnostics::session_direction::inbound, true) == 1;
      if (!relay_sessions_identified) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relayed session Identify");
      }
   }
   BOOST_REQUIRE(relay_sessions_identified);

   auto second_stream = forge::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), protocol, open_options));
   const auto second_payload = std::vector<std::uint8_t>{'r', 'e', 'u', 's', 'e'};
   forge::asio::blocking::run(runtime, second_stream.async_write_frame(second_payload));
   const auto second_reply = forge::asio::blocking::run(runtime, second_stream.async_read_frame());
   forge::asio::blocking::run(runtime, second_stream.async_close());

   BOOST_TEST(second_reply == second_payload, boost::test_tools::per_element());
   BOOST_TEST(relay_session_count(source, target.local_peer(), relay_node.local_peer(),
                                  diagnostics::session_direction::outbound, false) == 1);
   BOOST_TEST(relay_session_count(target, source.local_peer(), relay_node.local_peer(),
                                  diagnostics::session_direction::inbound, false) == 1);

   const auto pushed_protocol = protocol_id{.value = "/product/relay-identify-push/1"};
   register_echo(source, pushed_protocol);
   auto push_observed = false;
   for (auto attempt = 0U; attempt < 200U && !push_observed; ++attempt) {
      const auto source_record = target.peers().find(source.local_peer());
      push_observed = source_record && std::ranges::contains(source_record->protocols, pushed_protocol);
      if (!push_observed) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relayed Identify Push add");
      }
   }
   BOOST_REQUIRE(push_observed);

   BOOST_REQUIRE(source.unregister_protocol_handler(pushed_protocol));
   auto removal_observed = false;
   for (auto attempt = 0U; attempt < 200U && !removal_observed; ++attempt) {
      const auto source_record = target.peers().find(source.local_peer());
      removal_observed = source_record && !std::ranges::contains(source_record->protocols, pushed_protocol);
      if (!removal_observed) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "relayed Identify Push remove");
      }
   }
   BOOST_REQUIRE(removal_observed);

   forge::asio::blocking::run(runtime, target.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_document_roundtrips_libp2p_fields) {
   const auto id = peer(74);
   auto doc = identify::document{
       .protocol_version = "/forge/test/1",
       .agent_version = "forge-test/1",
       .public_key = std::vector<std::uint8_t>{1, 2, 3},
       .listen_endpoints =
           std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string())},
       .observed_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/5001/quic-v1/p2p/" + id.to_string()),
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
       .signed_peer_record = std::vector<std::uint8_t>{9, 8, 7},
   };

   auto decoded = identify::decode(identify::encode(doc));

   BOOST_TEST(decoded.protocol_version == doc.protocol_version);
   BOOST_TEST(decoded.agent_version == doc.agent_version);
   BOOST_TEST(decoded.public_key == doc.public_key, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(decoded.listen_endpoints.size(), 1U);
   BOOST_TEST(decoded.listen_endpoints.front().to_string() == doc.listen_endpoints.front().to_string());
   BOOST_REQUIRE(decoded.observed_endpoint.has_value());
   BOOST_TEST(decoded.observed_endpoint->to_string() == doc.observed_endpoint->to_string());
   BOOST_REQUIRE_EQUAL(decoded.protocols.size(), 2U);
   BOOST_TEST(decoded.protocols.front().value == builtins::ping.value);
   BOOST_TEST(decoded.signed_peer_record == doc.signed_peer_record, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(p2p_direct_nodes_negotiate_protocol_and_echo_frames) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(2))};
   auto client = node{runtime, options_for(peer(1))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   const auto session = forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo));
   const auto payload = std::vector<std::uint8_t>{'p', '2', 'p'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().protocol_streams_opened >= 1U);
   BOOST_TEST(server.metrics().protocol_streams_accepted >= 1U);
   BOOST_TEST(client.diagnostics().resources.active_streams == 1U);
   forge::asio::blocking::run(runtime, stream.async_close());
   BOOST_TEST(client.diagnostics().resources.active_streams == 0U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_inbound_stream_saturation_does_not_stop_session_acceptance) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(peer(249));
   server_options.limits.resources.max_streams = 1;
   server_options.limits.resources.max_streams_per_peer = 1;
   server_options.limits.resources.max_streams_per_protocol = 1;
   server_options.transport_limits.max_streams_per_connection = 1;
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, options_for(peer(250))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   auto identify_streams_closed = false;
   for (auto attempt = 0U; attempt < 100U && !identify_streams_closed; ++attempt) {
      identify_streams_closed =
          server.diagnostics().resources.active_streams == 0U && client.diagnostics().resources.active_streams == 0U;
      if (!identify_streams_closed) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "inbound stream Identify cleanup");
      }
   }
   BOOST_REQUIRE(identify_streams_closed);

   for (auto attempt = 0U; attempt < 2U; ++attempt) {
      auto stream =
          forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo));
      const auto payload = std::vector<std::uint8_t>{static_cast<std::uint8_t>(attempt + 1U)};
      forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
      BOOST_TEST(forge::asio::blocking::run(runtime, stream.async_read_frame()) == payload,
                 boost::test_tools::per_element());
      forge::asio::blocking::run(runtime, stream.async_close());
   }

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_node_queued_byte_budget_is_held_until_quic_ack) {
   auto server_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto client_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{server_runtime, options_for(peer(247))};
   auto client_options = options_for(peer(248));
   client_options.limits.resources.max_queued_bytes = 32 * 1024;
   client_options.identify.max_total_message_size = client_options.identify.max_message_size;
   auto client = node{client_runtime, std::move(client_options)};
   register_echo(server);

   const auto server_endpoint = listen(server, server_runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   static_cast<void>(forge::asio::blocking::run(
       client_runtime,
       client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   auto stream = forge::asio::blocking::run(
       client_runtime,
       client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                         node::open_options{.allow_relay = false, .allow_hole_punch = false}));

   auto release_server = block_runtime(server_runtime, "P2P queued-byte ACK barrier");
   const auto first = std::vector<std::uint8_t>(32 * 1024, 0x31);
   forge::asio::blocking::run(client_runtime, stream.async_write(first));
   BOOST_TEST(client.diagnostics().resources.queued_bytes == first.size());

   try {
      const auto extra = std::vector<std::uint8_t>{0x32};
      forge::asio::blocking::run(client_runtime, stream.async_write(extra));
      BOOST_FAIL("expected P2P node queued-byte rejection before transport enqueue");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   BOOST_TEST(client.diagnostics().resources.queued_bytes == first.size());

   release_server->set_value();
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (client.diagnostics().resources.queued_bytes != 0) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < deadline);
      wait_on_runtime(client_runtime, std::chrono::milliseconds{5}, "P2P queued-byte ACK release");
   }

   try {
      const auto oversized = std::vector<std::uint8_t>(16 * 1024 * 1024 + 1, 0x33);
      forge::asio::blocking::run(client_runtime, stream.async_write_frame(oversized));
      BOOST_FAIL("expected P2P byte admission before framed payload allocation");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   BOOST_TEST(client.diagnostics().resources.queued_bytes == 0U);

   stream.cancel();
   forge::asio::blocking::run(client_runtime, client.async_stop());
   forge::asio::blocking::run(server_runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_api_wire_v2_duplex_streams_over_direct_quic) {
   run_live_api_over(endpoint::protocol_kind::quic_v1);
}

BOOST_AUTO_TEST_CASE(p2p_api_wire_v2_duplex_streams_over_tcp_yamux) {
   run_live_api_over(endpoint::protocol_kind::tcp);
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_uses_endpoint_peer_when_expected_peer_is_absent) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(make_test_certificate_identity("quic-endpoint-peer-server"));
   auto client_options = options_for(make_test_certificate_identity("quic-endpoint-peer-client"));
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_state.persistence = peer_store::make_memory_persistence();
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   auto server_endpoint = listen(server, runtime);
   server_endpoint.peer = server.local_peer();
   const auto session = forge::asio::blocking::run(runtime, client.async_connect(server_endpoint));

   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_rejects_endpoint_peer_mismatch) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(make_test_certificate_identity("quic-endpoint-peer-mismatch-server"));
   auto client_options = options_for(make_test_certificate_identity("quic-endpoint-peer-mismatch-client"));
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_state.persistence = peer_store::make_memory_persistence();
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   auto server_endpoint = listen(server, runtime);
   server_endpoint.peer = peer(150);
   try {
      (void)forge::asio::blocking::run(runtime, client.async_connect(server_endpoint));
      BOOST_FAIL("expected QUIC endpoint peer mismatch");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_explicit_expected_peer_matches_certificate_peer_id) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(make_test_certificate_identity("quic-expected-peer-server"));
   auto client_options = options_for(make_test_certificate_identity("quic-expected-peer-client"));
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_state.persistence = peer_store::make_memory_persistence();
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto server_endpoint = listen(server, runtime);
   const auto session = forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_rejects_missing_certificate_identity_without_expected_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = legacy_cert_hash_peer_id(test_certificate()),
       .peer_state = peer_store::options{.persistence = peer_store::make_memory_persistence()},
       .allow_insecure_test_mode = true,
   };
   auto client_options = options_for(make_test_certificate_identity("quic-missing-extension-client"));
   client_options.allow_insecure_test_mode = false;
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto server_endpoint = listen(server, runtime);
   try {
      (void)forge::asio::blocking::run(runtime, client.async_connect(server_endpoint));
      BOOST_FAIL("expected missing QUIC certificate identity extension to fail");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_rejects_missing_certificate_identity_with_endpoint_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto legacy_peer = legacy_cert_hash_peer_id(test_certificate());
   auto server_options = node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = legacy_peer,
       .peer_state = peer_store::options{.persistence = peer_store::make_memory_persistence()},
       .allow_insecure_test_mode = true,
   };
   auto client_options = options_for(make_test_certificate_identity("quic-missing-extension-endpoint-client"));
   client_options.allow_insecure_test_mode = false;
   client_options.peer_state.persistence = peer_store::make_memory_persistence();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   auto server_endpoint = listen(server, runtime);
   server_endpoint.peer = legacy_peer;
   try {
      (void)forge::asio::blocking::run(runtime, client.async_connect(server_endpoint));
      BOOST_FAIL("expected missing QUIC certificate identity extension to fail");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_ping_protocol_uses_libp2p_payload_echo) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(72))};
   auto client = node{runtime, options_for(peer(73))};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::ping));
   const auto payload = std::vector<std::uint8_t>(32, 0x42);
   forge::asio::blocking::run(runtime, stream.async_write(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_ping_api_returns_rtt) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(82))};
   auto client = node{runtime, options_for(peer(83))};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto rtt = forge::asio::blocking::run(runtime, client.async_ping(server.local_peer()));
   BOOST_TEST(rtt.count() >= 0);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_protocol_advertises_supported_protocols) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(75))};
   auto client = node{runtime, options_for(peer(76))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto payload = forge::asio::blocking::run(runtime, read_length_delimited(stream));
   const auto doc = identify::decode(payload);

   BOOST_TEST(doc.agent_version == "forge/1.0.0");
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::ping; }));
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::identify; }));
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::echo; }));
   BOOST_TEST(!doc.listen_endpoints.empty());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_verified_identify_dht_advertisement_admits_server_before_exchange) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-dht-candidate-server");
   const auto client_identity = make_test_certificate_identity("identify-dht-candidate-client");
   auto server = node{runtime, options_for(server_identity, capability_set{.bits = capabilities::direct_quic})};
   server.register_protocol_handler(builtins::kad_dht,
                                    [](node::incoming_protocol_stream) -> boost::asio::awaitable<void> { co_return; });
   auto client_options = dht_options_for(client_identity, amino_v1());
   client_options.limits.discovery.rendezvous_enabled = false;
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   for (auto attempt = 0U; attempt < 20U && client.routing_status(builtins::kad_dht).active == 0U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "Identify DHT protocol fact");
   }
   const auto record = client.peers().find(server.local_peer());
   BOOST_REQUIRE(record.has_value());
   BOOST_TEST(
       std::ranges::any_of(record->protocols, [](const auto& protocol) { return protocol == builtins::kad_dht; }));
   BOOST_TEST(client.routing_status(builtins::kad_dht).active == 1U);
   BOOST_TEST(client.routing_status(builtins::kad_dht).candidates == 0U);
   BOOST_TEST(server.metrics().dht_queries == 0U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_discovery_candidate_probes_share_one_overall_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client_options = dht_options_for(make_test_identity(), amino_v1());
   client_options.limits.discovery.rendezvous_enabled = false;
   client_options.limits.discovery.max_parallel_queries = 4;
   client_options.limits.discovery.query_timeout = std::chrono::milliseconds{75};
   auto client = node{runtime, std::move(client_options)};
   for (const auto value : {22U, 23U, 24U, 25U}) {
      const auto candidate = peer(static_cast<std::uint8_t>(value));
      auto endpoint = start_stalling_tcp_peer(runtime);
      endpoint.peer = candidate;
      client.peers().upsert_routing_peer(
          builtins::kad_dht,
          dht::peer{.id = candidate,
                    .endpoints = std::vector<forge::net::p2p::endpoint>{std::move(endpoint)},
                    .connection = dht::connection_type::can_connect},
          discovery::source::explicit_config, std::chrono::system_clock::now() + std::chrono::hours{1});
   }
   forge::asio::blocking::run(runtime, client.peers().async_flush());
   forge::asio::blocking::run(runtime, client.async_hydrate_peer_state());

   const auto started = std::chrono::steady_clock::now();
   try {
      (void)forge::asio::blocking::run(runtime, client.async_refresh_discovery());
      BOOST_FAIL("expected bounded discovery timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) == static_cast<int>(exceptions::code::timeout));
   }
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   BOOST_TEST(elapsed.count() < 225);

   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_discovery_queries_verified_routing_seed_with_unverified_candidates) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 3}};
   const auto server_identity = make_test_certificate_identity("discovery-routing-server");
   const auto client_identity = make_test_certificate_identity("discovery-routing-client");
   auto server_options = dht_options_for(server_identity, amino_v1(dht::mode::server));
   auto client_options = dht_options_for(client_identity, amino_v1());
   client_options.limits.discovery.rendezvous_enabled = false;
   client_options.limits.discovery.max_parallel_queries = 1;
   client_options.limits.discovery.query_timeout = std::chrono::milliseconds{250};
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   verify_dht_server(runtime, client, server, server_endpoint, builtins::kad_dht);

   const auto stalled = peer(231);
   auto stalled_endpoint = start_stalling_tcp_peer(runtime, std::chrono::seconds{5});
   stalled_endpoint.peer = stalled;
   client.peers().upsert(peer_store::record{
       .peer = stalled,
       .protocols = std::vector<protocol_id>{builtins::kad_dht},
       .endpoints = std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{.endpoint = stalled_endpoint}},
       .discovery_expires_at = std::chrono::system_clock::now() + std::chrono::hours{1},
       .score = 1'000.0,
   });
   forge::asio::blocking::run(runtime, client.peers().async_flush());
   forge::asio::blocking::run(runtime, client.async_hydrate_peer_state());

   const auto queries_before = server.metrics().dht_queries;
   const auto started = std::chrono::steady_clock::now();
   try {
      static_cast<void>(forge::asio::blocking::run(runtime, client.async_refresh_discovery()));
      BOOST_FAIL("expected unverified DHT candidate timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) == static_cast<int>(exceptions::code::timeout));
   }
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

   BOOST_TEST(server.metrics().dht_queries > queries_before);
   BOOST_TEST(elapsed.count() < 500);
   const auto stalled_record = client.peers().find(stalled);
   BOOST_REQUIRE(stalled_record.has_value());
   BOOST_TEST(stalled_record->failures == 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_hydrated_candidate_bootstraps_lookup_without_refresh) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options = dht_options_for(peer(239), amino_v1(dht::mode::server));
   auto server = node{runtime, std::move(server_options)};
   const auto server_endpoint = listen(server, runtime);

   auto persistence = peer_store::make_memory_persistence();
   forge::asio::blocking::run(runtime,
                              persistence->async_apply(peer_store::mutation_batch{
                                  .peer_upserts = std::vector<peer_store::record>{peer_store::record{
                                      .peer = server.local_peer(),
                                      .discovered_by = discovery::source::explicit_config,
                                      .protocols = std::vector<protocol_id>{builtins::kad_dht},
                                      .endpoints = std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{
                                          .endpoint = server_endpoint}},
                                      .discovery_expires_at = std::chrono::system_clock::now() + std::chrono::hours{1},
                                  }},
                              }));
   auto client_options = dht_options_for(peer(240), amino_v1());
   client_options.peer_state.persistence = persistence;
   auto client = node{runtime, std::move(client_options)};
   forge::asio::blocking::run(runtime, client.async_hydrate_peer_state());

   BOOST_TEST(client.routing_status(builtins::kad_dht).active == 0U);
   BOOST_TEST(client.routing_status(builtins::kad_dht).candidates == 1U);
   const auto found =
       forge::asio::blocking::run(runtime, client.async_find_peer(builtins::kad_dht, server.local_peer()));
   BOOST_TEST(found.complete);
   BOOST_TEST(std::ranges::any_of(found.closest_peers,
                                  [&](const dht::peer& value) { return value.id == server.local_peer(); }));
   BOOST_TEST(server.metrics().dht_queries >= 1U);
   BOOST_TEST(client.routing_status(builtins::kad_dht).active == 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_put_value_local_store_does_not_satisfy_remote_quorum) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto local = node{runtime, dht_options_for(peer(62), custom_test_value_dht_profile())};
   const auto key = dht::key{.bytes = {'/', 'l', 'o', 'c', 'a', 'l'}};
   const auto value = std::vector<std::uint8_t>{'v', 'a', 'l', 'u', 'e'};

   auto pending = boost::asio::co_spawn(
       runtime.context(),
       local.async_put_value(content_swarm_value_test_dht, dht::record{.key_value = key, .value = value},
                             dht::query_options{.quorum = 1, .timeout = std::chrono::milliseconds{250}}),
       boost::asio::use_future);
   BOOST_REQUIRE(pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto result = pending.get();

   BOOST_TEST(result.selected.key_value.bytes == key.bytes, boost::test_tools::per_element());
   BOOST_TEST(result.selected.value == value, boost::test_tools::per_element());
   BOOST_TEST(result.accepted == 0U);
   BOOST_TEST(result.attempted == 0U);
   BOOST_TEST(!result.quorum_reached);

   auto read_pending = boost::asio::co_spawn(
       runtime.context(),
       local.async_get_value(content_swarm_value_test_dht, key,
                             dht::query_options{.quorum = 1, .timeout = std::chrono::milliseconds{250}}),
       boost::asio::use_future);
   BOOST_REQUIRE(read_pending.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   const auto stored = read_pending.get();
   BOOST_REQUIRE(stored.selected.has_value());
   BOOST_TEST(stored.selected->value == value, boost::test_tools::per_element());
   BOOST_TEST(stored.valid_records == 1U);
   BOOST_TEST(stored.quorum_reached);

   forge::asio::blocking::run(runtime, local.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_get_value_reports_remaining_record_ttl) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("dht-value-ttl-server");
   const auto client_identity = make_test_certificate_identity("dht-value-ttl-client");
   const auto server_profile = custom_test_value_dht_profile(dht::mode::server);
   const auto client_profile = custom_test_value_dht_profile();
   auto server = node{runtime, dht_options_for(server_identity, server_profile)};
   auto client = node{runtime, dht_options_for(client_identity, client_profile)};
   const auto server_endpoint = listen(server, runtime);
   verify_dht_server(runtime, client, server, server_endpoint, content_swarm_value_test_dht);

   const auto key = make_dht_key(std::vector<std::uint8_t>{'/', 't', 't', 'l'});
   const auto put = forge::asio::blocking::run(
       runtime, client.async_put_value(content_swarm_value_test_dht,
                                       dht::record{.key_value = key, .value = {'v'}, .ttl = std::chrono::seconds{2}}));
   BOOST_REQUIRE(put.quorum_reached);
   wait_on_runtime(runtime, std::chrono::milliseconds{1'100}, "DHT value TTL aging");

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), content_swarm_value_test_dht));
   forge::asio::blocking::run(
       runtime, stream.async_write(dht::codec::encode(
                    dht::message{.type = dht::message_type::get_value, .key_value = key}, client_profile)));
   const auto response = dht::codec::decode(
       wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream))), client_profile);
   BOOST_REQUIRE(response.record_value.has_value());
   BOOST_TEST(response.record_value->ttl == std::chrono::seconds{1});
   forge::asio::blocking::run(runtime, stream.async_close());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_node_creates_ipns_record_with_its_identity) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto identity = make_test_identity();
   auto local = node{runtime, options_for(identity)};
   const auto value = std::string_view{"/ipfs/bafkqaaa"};
   const auto eol = ipns::time_point{
       std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::sys_days{std::chrono::year{2030} / 1 / 1})};
   const auto record = local.create_ipns_record(
       std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}, 7, eol,
       std::chrono::minutes{5});

   BOOST_TEST(record.sequence() == 7U);
   BOOST_TEST(std::vector<std::uint8_t>(record.value().begin(), record.value().end()) ==
                  std::vector<std::uint8_t>(value.begin(), value.end()),
              boost::test_tools::per_element());
   ipns::validate(record, local.local_peer(), identity.key,
                  ipns::time_point{std::chrono::time_point_cast<std::chrono::seconds>(
                      std::chrono::sys_days{std::chrono::year{2029} / 1 / 1})});

   forge::asio::blocking::run(runtime, local.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_find_node_returns_exact_peer_store_and_self_before_active_closest) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto limits = dht::options{};
   limits.replication = 2;
   limits.alpha = 1;
   const auto server_profile = custom_test_dht_profile(dht::mode::server, limits);
   const auto client_profile = custom_test_dht_profile(dht::mode::client, limits);
   const auto server_identity = make_test_certificate_identity("dht-find-node-server");
   const auto active_identity = make_test_certificate_identity("dht-find-node-active");
   const auto client_identity = make_test_certificate_identity("dht-find-node-client");
   auto server_options = dht_options_for(server_identity, server_profile);
   server_options.limits.discovery.rendezvous_enabled = false;
   auto active_options = dht_options_for(active_identity, server_profile);
   auto server = node{runtime, std::move(server_options)};
   auto active = node{runtime, std::move(active_options)};
   auto client = node{runtime, dht_options_for(client_identity, client_profile)};
   const auto server_endpoint = listen(server, runtime);
   const auto active_endpoint = listen(active, runtime);
   verify_dht_server(runtime, server, active, active_endpoint, content_swarm_test_dht);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   const auto exact = peer(29);
   auto exact_endpoint = make_dns_tcp_endpoint(4029, "exact-find-node.example.com");
   exact_endpoint.peer = exact;
   server.peers().learn_endpoint(exact, exact_endpoint);

   const auto exact_response =
       exchange_find_node(runtime, client, server.local_peer(), client_profile, make_dht_key(exact));
   BOOST_REQUIRE_EQUAL(exact_response.closer_peers.size(), 2U);
   BOOST_TEST(exact_response.closer_peers.front().id.to_string() == exact.to_string());
   BOOST_REQUIRE_EQUAL(exact_response.closer_peers.front().endpoints.size(), 1U);
   BOOST_TEST(exact_response.closer_peers.front().endpoints.front().to_string() == exact_endpoint.to_string());
   BOOST_TEST(exact_response.closer_peers[1].id.to_string() == active.local_peer().to_string());

   const auto self_response =
       exchange_find_node(runtime, client, server.local_peer(), client_profile, make_dht_key(server.local_peer()));
   BOOST_REQUIRE_EQUAL(self_response.closer_peers.size(), 2U);
   BOOST_TEST(self_response.closer_peers.front().id.to_string() == server.local_peer().to_string());
   BOOST_TEST(!self_response.closer_peers.front().endpoints.empty());
   BOOST_TEST(self_response.closer_peers[1].id.to_string() == active.local_peer().to_string());

   const auto arbitrary_response = exchange_find_node(runtime, client, server.local_peer(), client_profile,
                                                      make_dht_key(std::vector<std::uint8_t>{0xffU}));
   BOOST_REQUIRE_EQUAL(arbitrary_response.closer_peers.size(), 1U);
   BOOST_TEST(arbitrary_response.closer_peers.front().id.to_string() == active.local_peer().to_string());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, active.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_node_finds_peer_and_provider_over_negotiated_stream) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("dht-node-server");
   const auto client_identity = make_test_certificate_identity("dht-node-client");
   const auto target_identity = make_test_certificate_identity("dht-node-target");
   const auto provider_identity = make_test_certificate_identity("dht-node-provider");
   auto server_options = dht_options_for(server_identity, amino_v1(dht::mode::server));
   auto server_persistence = std::make_shared<tracking_dht_record_store_persistence>();
   server_options.dht_record_persistence.emplace(builtins::kad_dht, server_persistence);
   auto client_options = dht_options_for(client_identity, amino_v1());
   auto target_options = dht_options_for(target_identity, amino_v1(dht::mode::server));
   auto provider_endpoint = make_dns_tcp_endpoint(4121, "provider.example.com");
   provider_endpoint.peer = provider_identity.peer;
   auto provider_options = dht_options_for(provider_identity, amino_v1());
   provider_options.advertised_endpoints.push_back(provider_endpoint);
   auto provider_persistence = std::make_shared<tracking_dht_record_store_persistence>();
   provider_options.dht_record_persistence.emplace(builtins::kad_dht, provider_persistence);
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   auto target_node = node{runtime, std::move(target_options)};
   auto provider_node = node{runtime, std::move(provider_options)};
   const auto server_endpoint = listen(server, runtime);
   const auto target_listen_endpoint = listen(target_node, runtime);
   static_cast<void>(listen(provider_node, runtime));
   auto target_endpoint = make_dns_tcp_endpoint(4120, "target.example.com");
   target_endpoint.peer = target_node.local_peer();
   verify_dht_server(runtime, server, target_node, target_listen_endpoint, builtins::kad_dht, target_endpoint);
   verify_dht_server(runtime, client, server, server_endpoint, builtins::kad_dht);
   verify_dht_server(runtime, provider_node, server, server_endpoint, builtins::kad_dht);
   forge::asio::blocking::run(runtime, provider_node.async_hydrate_peer_state());

   const auto target = target_node.local_peer();
   const auto provider = provider_node.local_peer();
   const auto key =
       amino_provider_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 'n', 'o', 'd', 'e'});
   auto endpoints_changed_during_commit = false;
   provider_persistence->before_provider_apply = [&] {
      provider_node.set_advertised_endpoints({});
      endpoints_changed_during_commit = true;
   };
   provider_persistence->reject_provider_upserts_after_first = true;
   auto provider_registration =
       forge::asio::blocking::run(runtime, provider_node.async_provide(builtins::kad_dht, key));
   BOOST_REQUIRE(provider_registration.active());
   BOOST_TEST(endpoints_changed_during_commit);
   const auto publications_after_initial_provide =
       server_persistence->provider_upsert_attempts.load(std::memory_order_relaxed);
   auto coalesced_registration =
       forge::asio::blocking::run(runtime, provider_node.async_provide(builtins::kad_dht, key));
   BOOST_REQUIRE(coalesced_registration.active());
   BOOST_TEST(provider_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) >= 1U);
   BOOST_TEST(server_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) ==
              publications_after_initial_provide);

   const auto found_peer = forge::asio::blocking::run(runtime, client.async_find_peer(builtins::kad_dht, target));
   BOOST_TEST(std::ranges::any_of(found_peer.closest_peers, [&](const dht::peer& value) {
      return value.id == target && !value.endpoints.empty() &&
             value.endpoints.front().to_string() == target_endpoint.to_string();
   }));

   const auto providers = forge::asio::blocking::run(runtime, client.async_find_providers(builtins::kad_dht, key));
   BOOST_TEST(std::ranges::any_of(providers, [&](const dht::peer& value) {
      return value.id == provider && std::ranges::any_of(value.endpoints, [&](const endpoint& current) {
                return current.to_string() == provider_endpoint.to_string();
             });
   }));
   BOOST_TEST(server.metrics().dht_queries >= 2U);
   BOOST_TEST(server.metrics().dht_responses >= 2U);

   forge::asio::blocking::run(runtime, provider_registration.async_withdraw());
   BOOST_TEST(!provider_registration.active());
   BOOST_TEST(coalesced_registration.active());
   BOOST_TEST(provider_persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 0U);
   forge::asio::blocking::run(runtime, provider_registration.async_withdraw());
   BOOST_TEST(provider_persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 0U);
   forge::asio::blocking::run(runtime, coalesced_registration.async_withdraw());
   BOOST_TEST(!coalesced_registration.active());
   BOOST_TEST(provider_persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 1U);
   forge::asio::blocking::run(runtime, coalesced_registration.async_withdraw());
   BOOST_TEST(provider_persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, provider_node.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
   forge::asio::blocking::run(runtime, target_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_provide_requires_quorum_and_rolls_back_local_record) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_profile = custom_test_dht_profile(dht::mode::server);
   const auto provider_profile = custom_test_dht_profile(dht::mode::client);
   const auto server_identity = make_test_certificate_identity("dht-quorum-server");
   const auto provider_identity = make_test_certificate_identity("dht-quorum-provider");
   auto server = node{runtime, dht_options_for(server_identity, server_profile)};
   auto persistence = std::make_shared<tracking_dht_record_store_persistence>();
   auto provider_options = dht_options_for(provider_identity, provider_profile);
   provider_options.dht_record_persistence.emplace(content_swarm_test_dht, persistence);
   auto provider_node = node{runtime, std::move(provider_options)};
   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(listen(provider_node, runtime));
   verify_dht_server(runtime, provider_node, server, server_endpoint, content_swarm_test_dht);
   forge::asio::blocking::run(runtime, provider_node.async_hydrate_peer_state());

   const auto key = make_dht_key(
       std::vector<std::uint8_t>{'q', 'u', 'o', 'r', 'u', 'm', '-', 'r', 'o', 'l', 'l', 'b', 'a', 'c', 'k'});
   BOOST_CHECK_THROW((forge::asio::blocking::run(
                         runtime, provider_node.async_provide(content_swarm_test_dht, key,
                                                              dht::query_options{.requested_count = 2, .quorum = 2}))),
                     exceptions::peer_not_found);
   BOOST_TEST(persistence->provider_upsert_attempts.load(std::memory_order_relaxed) == 1U);
   BOOST_TEST(persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 1U);

   forge::asio::blocking::run(runtime, provider_node.async_stop());
   BOOST_TEST(persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 1U);
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_provide_replicates_to_all_closest_peers_after_quorum) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto first_identity = make_test_certificate_identity("dht-fanout-first");
   const auto second_identity = make_test_certificate_identity("dht-fanout-second");
   const auto provider_identity = make_test_certificate_identity("dht-fanout-provider");
   auto first_persistence = std::make_shared<tracking_dht_record_store_persistence>();
   auto second_persistence = std::make_shared<tracking_dht_record_store_persistence>();
   auto first_options = dht_options_for(first_identity, custom_test_dht_profile(dht::mode::server));
   auto second_options = dht_options_for(second_identity, custom_test_dht_profile(dht::mode::server));
   first_options.dht_record_persistence.emplace(content_swarm_test_dht, first_persistence);
   second_options.dht_record_persistence.emplace(content_swarm_test_dht, second_persistence);
   auto provider_options = dht_options_for(
       provider_identity, custom_test_dht_profile(dht::mode::client, dht::options{.replication = 2, .alpha = 1}));
   auto advertised = make_dns_tcp_endpoint(4'194, "dht-fanout-provider.example.com");
   advertised.peer = provider_identity.peer;
   provider_options.advertised_endpoints.push_back(advertised);

   auto first = node{runtime, std::move(first_options)};
   auto second = node{runtime, std::move(second_options)};
   auto provider = node{runtime, std::move(provider_options)};
   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   static_cast<void>(listen(provider, runtime));
   verify_dht_server(runtime, provider, first, first_endpoint, content_swarm_test_dht);
   verify_dht_server(runtime, provider, second, second_endpoint, content_swarm_test_dht);
   for (auto attempt = 0U; attempt < 20U && provider.routing_status(content_swarm_test_dht).active < 2U; ++attempt) {
      wait_on_runtime(runtime, std::chrono::milliseconds{50}, "DHT fanout peer admission");
   }
   BOOST_REQUIRE(provider.routing_status(content_swarm_test_dht).active == 2U);
   forge::asio::blocking::run(runtime, provider.async_hydrate_peer_state());

   const auto key = make_dht_key(std::vector<std::uint8_t>{'f', 'u', 'l', 'l', '-', 'f', 'a', 'n', 'o', 'u', 't'});
   for (auto attempt = 0U;
        attempt < 8U && (first_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) == 0U ||
                         second_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) == 0U);
        ++attempt) {
      auto registration = forge::asio::blocking::run(
          runtime, provider.async_provide(content_swarm_test_dht, key, dht::query_options{.quorum = 1}));
      BOOST_REQUIRE(registration.active());
      forge::asio::blocking::run(runtime, registration.async_withdraw());
   }
   BOOST_TEST(first_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) > 0U);
   BOOST_TEST(second_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) > 0U);

   forge::asio::blocking::run(runtime, provider.async_stop());
   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_get_providers_does_not_persist_third_party_claim) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto limits = dht::options{};
   limits.failure_threshold = 1;
   const auto server_profile = custom_test_dht_profile(dht::mode::server, limits);
   const auto client_profile = custom_test_dht_profile(dht::mode::client, limits);
   const auto server_identity = make_test_certificate_identity("dht-backpressure-server");
   const auto client_identity = make_test_certificate_identity("dht-backpressure-client");
   const auto provider_identity = make_test_certificate_identity("dht-backpressure-provider");
   auto server_options = dht_options_for(server_identity, server_profile);
   auto client_options = dht_options_for(client_identity, client_profile);
   auto client_persistence = std::make_shared<tracking_dht_record_store_persistence>();
   client_persistence->reject_provider_upserts = true;
   client_options.dht_record_persistence.emplace(content_swarm_test_dht, client_persistence);
   auto provider_options = dht_options_for(provider_identity, client_profile);
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   auto provider_node = node{runtime, std::move(provider_options)};
   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(listen(provider_node, runtime));
   verify_dht_server(runtime, client, server, server_endpoint, content_swarm_test_dht);
   verify_dht_server(runtime, provider_node, server, server_endpoint, content_swarm_test_dht);
   forge::asio::blocking::run(runtime, provider_node.async_hydrate_peer_state());

   const auto key = make_dht_key(std::vector<std::uint8_t>{'r', 'e', 'm', 'o', 't', 'e', '-', 'p', 'r', 'o', 'v'});
   auto provider_registration =
       forge::asio::blocking::run(runtime, provider_node.async_provide(content_swarm_test_dht, key));
   BOOST_REQUIRE(provider_registration.active());

   const auto routing_before = client.routing_status(content_swarm_test_dht);
   const auto server_before = client.peers().find(server.local_peer());
   const auto queries_before = server.metrics().dht_queries;
   const auto responses_before = server.metrics().dht_responses;
   BOOST_REQUIRE(server_before.has_value());
   BOOST_REQUIRE(routing_before.active > 0U);

   const auto discovered =
       forge::asio::blocking::run(runtime, client.async_find_providers(content_swarm_test_dht, key));

   const auto server_after = client.peers().find(server.local_peer());
   BOOST_REQUIRE(server_after.has_value());
   BOOST_TEST(
       std::ranges::any_of(discovered, [&](const auto& value) { return value.id == provider_node.local_peer(); }));
   BOOST_TEST(client_persistence->provider_upsert_attempts.load(std::memory_order_relaxed) == 0U);
   BOOST_TEST(server.metrics().dht_queries > queries_before);
   BOOST_TEST(server.metrics().dht_responses > responses_before);
   BOOST_TEST(server_after->failures == server_before->failures);
   BOOST_TEST(client.routing_status(content_swarm_test_dht).active == routing_before.active);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, provider_node.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_iterative_lookup_walks_many_peer_topology) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto seed_identity = make_test_certificate_identity("dht-seed");
   const auto hop_identity = make_test_certificate_identity("dht-hop");
   const auto target_identity = make_test_certificate_identity("dht-target");
   const auto client_identity = make_test_certificate_identity("dht-client");
   auto limits = dht::options{};
   limits.alpha = 1;
   const auto server_profile = custom_test_dht_profile(dht::mode::server, limits);
   const auto client_profile = custom_test_dht_profile(dht::mode::client, limits);
   auto seed_options = dht_options_for(seed_identity, server_profile);
   auto hop_options = dht_options_for(hop_identity, server_profile);
   auto target_options = dht_options_for(target_identity, server_profile);
   auto client_options = dht_options_for(client_identity, client_profile);

   auto seed = node{runtime, std::move(seed_options)};
   auto hop = node{runtime, std::move(hop_options)};
   auto target_node = node{runtime, std::move(target_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto seed_endpoint = listen(seed, runtime);
   const auto hop_endpoint = listen(hop, runtime);
   const auto target_listen_endpoint = listen(target_node, runtime);

   auto target_endpoint = make_dns_tcp_endpoint(4127, "target.example.com");
   target_endpoint.peer = target_node.local_peer();
   verify_dht_server(runtime, hop, target_node, target_listen_endpoint, content_swarm_test_dht, target_endpoint);

   auto hop_discovery_endpoint = make_dns_tcp_endpoint(4126, "hop.example.com");
   hop_discovery_endpoint.peer = hop.local_peer();
   verify_dht_server(runtime, seed, hop, hop_endpoint, content_swarm_test_dht, hop_discovery_endpoint);
   client.peers().learn_endpoint(hop.local_peer(), hop_endpoint, capability_set{});
   verify_dht_server(runtime, client, seed, seed_endpoint, content_swarm_test_dht);

   const auto target = target_node.local_peer();

   const auto found = forge::asio::blocking::run(runtime, client.async_find_peer(content_swarm_test_dht, target));
   BOOST_TEST(found.complete);
   BOOST_TEST(std::ranges::any_of(found.closest_peers, [&](const dht::peer& value) {
      return value.id == target && !value.endpoints.empty() &&
             value.endpoints.front().to_string() == target_endpoint.to_string();
   }));
   BOOST_TEST(seed.metrics().dht_queries >= 1U);
   BOOST_TEST(hop.metrics().dht_queries >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, target_node.async_stop());
   forge::asio::blocking::run(runtime, hop.async_stop());
   forge::asio::blocking::run(runtime, seed.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_iterative_provider_lookup_and_provide_reach_closest_peers) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto seed_identity = make_test_certificate_identity("dht-provider-seed");
   const auto hop_identity = make_test_certificate_identity("dht-provider-hop");
   const auto client_identity = make_test_certificate_identity("dht-provider-client");
   const auto provider_identity = make_test_certificate_identity("dht-provider-origin");
   auto limits = dht::options{};
   limits.alpha = 1;
   const auto server_profile = custom_test_dht_profile(dht::mode::server, limits);
   const auto client_profile = custom_test_dht_profile(dht::mode::client, limits);
   auto seed_options = dht_options_for(seed_identity, server_profile);
   auto hop_options = dht_options_for(hop_identity, server_profile);
   auto client_options = dht_options_for(client_identity, client_profile);
   auto provider_endpoint = make_dns_tcp_endpoint(4131, "provider.example.com");
   provider_endpoint.peer = provider_identity.peer;
   auto provider_options = dht_options_for(provider_identity, client_profile);
   provider_options.advertised_endpoints.push_back(provider_endpoint);

   auto seed = node{runtime, std::move(seed_options)};
   auto hop = node{runtime, std::move(hop_options)};
   auto client = node{runtime, std::move(client_options)};
   auto provider_node = node{runtime, std::move(provider_options)};
   const auto seed_endpoint = listen(seed, runtime);
   const auto hop_endpoint = listen(hop, runtime);
   const auto client_endpoint = listen(client, runtime);
   static_cast<void>(client_endpoint);
   static_cast<void>(listen(provider_node, runtime));
   auto hop_discovery_endpoint = make_dns_tcp_endpoint(4130, "provider-hop.example.com");
   hop_discovery_endpoint.peer = hop.local_peer();
   verify_dht_server(runtime, seed, hop, hop_endpoint, content_swarm_test_dht, hop_discovery_endpoint);
   client.peers().learn_endpoint(hop.local_peer(), hop_endpoint, capability_set{});
   verify_dht_server(runtime, client, seed, seed_endpoint, content_swarm_test_dht);
   verify_dht_server(runtime, provider_node, hop, hop_endpoint, content_swarm_test_dht);
   forge::asio::blocking::run(runtime, provider_node.async_hydrate_peer_state());
   forge::asio::blocking::run(runtime, client.async_hydrate_peer_state());

   const auto key = make_dht_key(
       std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 'm', 'u', 'l', 't', 'i', '-', 'h', 'o', 'p'});
   const auto provider = provider_node.local_peer();
   auto provider_registration =
       forge::asio::blocking::run(runtime, provider_node.async_provide(content_swarm_test_dht, key));
   BOOST_REQUIRE(provider_registration.active());

   const auto providers = forge::asio::blocking::run(runtime, client.async_find_providers(content_swarm_test_dht, key));
   BOOST_TEST(std::ranges::any_of(providers, [&](const dht::peer& value) {
      return value.id == provider && std::ranges::any_of(value.endpoints, [&](const endpoint& current) {
                return current.to_string() == provider_endpoint.to_string();
             });
   }));

   const auto publish_key =
       make_dht_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 'r', 'e', 'p', 'u', 'b'});
   auto client_registration =
       forge::asio::blocking::run(runtime, client.async_provide(content_swarm_test_dht, publish_key));
   BOOST_REQUIRE(client_registration.active());
   auto stored = std::vector<dht::peer>{};
   for (auto attempt = 0; attempt < 100; ++attempt) {
      stored = forge::asio::blocking::run(runtime, hop.async_find_providers(content_swarm_test_dht, publish_key,
                                                                            dht::query_options{.requested_count = 1}));
      if (std::ranges::any_of(stored, [&](const dht::peer& value) { return value.id == client.local_peer(); })) {
         break;
      }
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "DHT provider publication convergence");
   }
   BOOST_TEST(std::ranges::any_of(stored, [&](const dht::peer& value) { return value.id == client.local_peer(); }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, provider_node.async_stop());
   forge::asio::blocking::run(runtime, hop.async_stop());
   forge::asio::blocking::run(runtime, seed.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_node_registers_and_discovers_over_negotiated_stream) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options =
       options_for(peer(122), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   server_options.limits.rendezvous.require_signed_peer_record = false;
   auto client_options =
       options_for(peer(123), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   client_options.limits.rendezvous.require_signed_peer_record = false;
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   const auto response = forge::asio::blocking::run(
       runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
   BOOST_TEST(static_cast<int>(response.status_value) == static_cast<int>(rendezvous::status::ok));
   BOOST_TEST(response.ttl == std::chrono::seconds{7'200});

   const auto discovered = forge::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .limit = 10,
                                                                      }));
   BOOST_TEST(static_cast<int>(discovered.status_value) == static_cast<int>(rendezvous::status::ok));
   BOOST_REQUIRE_EQUAL(discovered.registrations.size(), 1U);
   BOOST_TEST(discovered.registrations.front().namespace_name == "forge.discovery");
   BOOST_TEST(discovered.registrations.front().peer.to_string().empty());
   BOOST_TEST(discovered.registrations.front().signed_peer_record.empty());
   BOOST_TEST(rendezvous::codec::read_cookie(discovered.cookie) >= 1U);
   BOOST_TEST(server.metrics().rendezvous_registrations >= 1U);
   BOOST_TEST(server.metrics().rendezvous_discovers >= 1U);

   const auto invalid_cookie = forge::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(),
                                                 rendezvous::discover_request{
                                                     .namespace_name = "forge.discovery",
                                                     .limit = 10,
                                                     .cookie = rendezvous::codec::make_cookie(1, "forge.other"),
                                                 }));
   BOOST_TEST(static_cast<int>(invalid_cookie.status_value) == static_cast<int>(rendezvous::status::invalid_cookie));
   BOOST_TEST(invalid_cookie.registrations.empty());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_server_enforces_per_peer_registration_capacity) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options =
       options_for(peer(224), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   server_options.limits.rendezvous.max_registrations_per_peer = 1;
   const auto first_identity = make_test_certificate_identity("rendezvous-capacity-first");
   const auto second_identity = make_test_certificate_identity("rendezvous-capacity-second");
   auto first_options =
       options_for(first_identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   auto second_options =
       options_for(second_identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   auto server = node{runtime, std::move(server_options)};
   auto first = node{runtime, std::move(first_options)};
   auto second = node{runtime, std::move(second_options)};
   const auto server_endpoint = listen(server, runtime);
   first.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   second.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   auto first_endpoint = make_dns_tcp_endpoint(4'225, "rendezvous-capacity-first.example.com");
   first_endpoint.peer = first.local_peer();
   auto second_endpoint = make_dns_tcp_endpoint(4'226, "rendezvous-capacity-second.example.com");
   second_endpoint.peer = second.local_peer();
   const auto first_record =
       make_signed_rendezvous_peer_record(first_identity, std::vector<endpoint>{std::move(first_endpoint)}, 1);
   const auto second_record =
       make_signed_rendezvous_peer_record(second_identity, std::vector<endpoint>{std::move(second_endpoint)}, 1);

   const auto register_namespace = [&](node& client, std::string namespace_name,
                                       const std::vector<std::uint8_t>& signed_peer_record) {
      return forge::asio::blocking::run(
          runtime,
          client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                    .namespace_name = std::move(namespace_name),
                                                                    .signed_peer_record = signed_peer_record,
                                                                    .ttl = std::chrono::seconds{7'200},
                                                                }));
   };
   BOOST_TEST(static_cast<int>(register_namespace(first, "forge.first", first_record).status_value) ==
              static_cast<int>(rendezvous::status::ok));
   BOOST_TEST(static_cast<int>(register_namespace(first, "forge.first", first_record).status_value) ==
              static_cast<int>(rendezvous::status::ok));
   BOOST_TEST(static_cast<int>(register_namespace(first, "forge.second", first_record).status_value) ==
              static_cast<int>(rendezvous::status::unavailable));
   BOOST_TEST(static_cast<int>(register_namespace(second, "forge.second", second_record).status_value) ==
              static_cast<int>(rendezvous::status::ok));

   forge::asio::blocking::run(runtime, second.async_stop());
   forge::asio::blocking::run(runtime, first.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_request_uses_one_deadline_after_protocol_negotiation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->block_apply = true;
   auto server_options =
       options_for(peer(124), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.peer_state.persistence = persistence;
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   server_options.limits.rendezvous.require_signed_peer_record = false;
   auto client_options =
       options_for(peer(125), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   client_options.limits.discovery.query_timeout = std::chrono::milliseconds{75};
   client_options.limits.rendezvous.require_signed_peer_record = false;
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   const auto started = std::chrono::steady_clock::now();
   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                             .namespace_name = "forge.timeout",
                                                                             .ttl = std::chrono::seconds{7'200},
                                                                         }));
      BOOST_FAIL("expected Rendezvous response timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) == static_cast<int>(exceptions::code::timeout));
   }
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   BOOST_TEST(elapsed.count() < 225);
   BOOST_TEST(persistence->wait_until_apply_blocked());

   persistence->release_apply();
   persistence->block_apply = false;
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_refresh_does_not_penalize_server_for_local_persistence_failure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 3}};
   const auto server_identity = make_test_certificate_identity("rendezvous-local-persistence-server");
   const auto registrar_identity = make_test_certificate_identity("rendezvous-local-persistence-registrar");
   const auto client_identity = make_test_certificate_identity("rendezvous-local-persistence-client");
   auto server_options =
       options_for(server_identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   auto registrar_options =
       options_for(registrar_identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto client_options =
       options_for(client_identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   client_options.peer_state.persistence = persistence;
   client_options.limits.discovery.dht_enabled = false;
   client_options.limits.discovery.rendezvous_namespaces = {"forge.persistence"};
   auto server = node{runtime, std::move(server_options)};
   auto registrar = node{runtime, std::move(registrar_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(listen(registrar, runtime));

   registrar.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                    capability_set{.bits = capabilities::rendezvous});
   auto advertised_registrar_endpoint = make_dns_tcp_endpoint(4139, "registrar.example.com");
   advertised_registrar_endpoint.peer = registrar.local_peer();
   const auto signed_record =
       make_signed_rendezvous_peer_record(registrar_identity, std::vector<endpoint>{advertised_registrar_endpoint}, 1);
   const auto registered = forge::asio::blocking::run(
       runtime, registrar.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                             .namespace_name = "forge.persistence",
                                                                             .signed_peer_record = signed_record,
                                                                             .ttl = std::chrono::seconds{7'200},
                                                                         }));
   BOOST_REQUIRE(registered.status_value == rendezvous::status::ok);

   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   const auto before = client.peers().find(server.local_peer());
   BOOST_REQUIRE(before.has_value());
   const auto apply_attempts_before = persistence->apply_attempts;
   persistence->fail_apply = true;
   BOOST_CHECK_THROW(static_cast<void>(forge::asio::blocking::run(runtime, client.async_refresh_discovery())),
                     std::runtime_error);
   const auto after = client.peers().find(server.local_peer());
   BOOST_REQUIRE(after.has_value());
   BOOST_TEST(after->failures == before->failures);
   BOOST_TEST(persistence->apply_attempts > apply_attempts_before);
   BOOST_TEST(client.diagnostics().persistence.degraded);

   persistence->fail_apply = false;
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, registrar.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_refresh_replaces_registration_and_cookie_discovers_new_records) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server_options =
       options_for(peer(132), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   auto identity = make_test_certificate_identity("rendezvous-refresh-client");
   auto client_options =
       options_for(identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   auto first_endpoint = make_dns_tcp_endpoint(4133, "client-first.example.com");
   first_endpoint.peer = client.local_peer();
   auto second_endpoint = make_dns_tcp_endpoint(4134, "client-second.example.com");
   second_endpoint.peer = client.local_peer();
   const auto first_record = make_signed_rendezvous_peer_record(identity, std::vector<endpoint>{first_endpoint}, 1);
   const auto second_record = make_signed_rendezvous_peer_record(identity, std::vector<endpoint>{second_endpoint}, 2);

   auto first = forge::asio::blocking::run(
       runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .signed_peer_record = first_record,
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
   BOOST_TEST(static_cast<int>(first.status_value) == static_cast<int>(rendezvous::status::ok));

   const auto first_discovery_started = std::chrono::system_clock::now();
   const auto after_first = forge::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .limit = 10,
                                                                      }));
   BOOST_REQUIRE_EQUAL(after_first.registrations.size(), 1U);
   BOOST_TEST(after_first.registrations.front().expires_at >= first_discovery_started + std::chrono::seconds{7'200});
   BOOST_TEST(after_first.registrations.front().expires_at <=
              std::chrono::system_clock::now() + std::chrono::seconds{7'200});
   const auto cached_after_first = client.peers().discover_rendezvous("forge.discovery", 0, 10);
   BOOST_REQUIRE_EQUAL(cached_after_first.size(), 1U);
   BOOST_TEST(cached_after_first.front().expires_at == after_first.registrations.front().expires_at);
   static_cast<void>(forge::asio::blocking::run(runtime, client.peers().async_prune_expired()));
   BOOST_REQUIRE_EQUAL(client.peers().discover_rendezvous("forge.discovery", 0, 10).size(), 1U);
   const auto cookie = after_first.cookie;

   auto second = forge::asio::blocking::run(
       runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .signed_peer_record = second_record,
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
   BOOST_TEST(static_cast<int>(second.status_value) == static_cast<int>(rendezvous::status::ok));

   const auto after_cookie = forge::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "forge.discovery",
                                                                          .limit = 10,
                                                                          .cookie = cookie,
                                                                      }));
   BOOST_REQUIRE_EQUAL(after_cookie.registrations.size(), 1U);
   BOOST_TEST(after_cookie.registrations.front().peer.to_string() == client.local_peer().to_string());
   BOOST_REQUIRE_EQUAL(after_cookie.registrations.front().endpoints.size(), 1U);
   BOOST_TEST(after_cookie.registrations.front().endpoints.front().to_string() == second_endpoint.to_string());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_discovery_refresh_learns_dht_and_rendezvous_relay_candidates_for_autorelay) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto dht_identity = make_test_certificate_identity("discovery-refresh-dht");
   auto client_identity = make_test_certificate_identity("discovery-refresh-client");
   auto relay_identity = make_test_certificate_identity("discovery-refresh-relay");
   auto dht_options = dht_options_for(dht_identity, amino_v1(dht::mode::server));
   auto rendezvous_options =
       options_for(peer(134), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   rendezvous_options.limits.rendezvous.operating_role = rendezvous::role::server;
   auto relay_options = dht_options_for(
       relay_identity, amino_v1(dht::mode::server),
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   auto client_options = dht_options_for(
       client_identity, amino_v1(),
       capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous | capabilities::relay_reservation});
   client_options.relay_policy.auto_discovery_enabled = true;
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 4;
   client_options.limits.discovery.max_results = 4;

   auto dht_server = node{runtime, std::move(dht_options)};
   auto rendezvous_server = node{runtime, std::move(rendezvous_options)};
   auto relay = node{runtime, std::move(relay_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto dht_endpoint = listen(dht_server, runtime);
   const auto rendezvous_endpoint = listen(rendezvous_server, runtime);
   const auto relay_endpoint = listen(relay, runtime);
   auto advertised_relay_endpoint = make_dns_tcp_endpoint(4140, "relay.example.com");
   advertised_relay_endpoint.peer = relay.local_peer();

   verify_dht_server(runtime, dht_server, relay, relay_endpoint, builtins::kad_dht, advertised_relay_endpoint);
   relay.peers().learn_endpoint(rendezvous_server.local_peer(), rendezvous_endpoint,
                                capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   const auto relay_record =
       make_signed_rendezvous_peer_record(relay_identity, std::vector<endpoint>{advertised_relay_endpoint}, 1);
   auto registered = forge::asio::blocking::run(
       runtime, relay.async_rendezvous_register(rendezvous_server.local_peer(), rendezvous::register_request{
                                                                                    .namespace_name = "forge.discovery",
                                                                                    .signed_peer_record = relay_record,
                                                                                    .ttl = std::chrono::seconds{7'200},
                                                                                }));
   BOOST_TEST(static_cast<int>(registered.status_value) == static_cast<int>(rendezvous::status::ok));

   verify_dht_server(runtime, client, dht_server, dht_endpoint, builtins::kad_dht);
   client.peers().learn_endpoint(rendezvous_server.local_peer(), rendezvous_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   client.peers().learn_endpoint(relay.local_peer(), relay_endpoint, capability_set{});

   const auto discovered = forge::asio::blocking::run(runtime, client.async_refresh_discovery());
   BOOST_TEST(std::ranges::any_of(discovered,
                                  [&](const discovery::result& value) { return value.peer == relay.local_peer(); }));
   const auto learned = client.peers().find(relay.local_peer());
   BOOST_REQUIRE(learned.has_value());
   BOOST_TEST(learned->capabilities.has(capabilities::relay));
   BOOST_TEST(learned->capabilities.has(capabilities::relay_reservation));

   const auto reservations = forge::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(reservations.front().relay_peer.to_string() == relay.local_peer().to_string());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, relay.async_stop());
   forge::asio::blocking::run(runtime, rendezvous_server.async_stop());
   forge::asio::blocking::run(runtime, dht_server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_nodes_deliver_signed_publish_over_negotiated_stream) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   subscriber_options.explicit_peer_id = peer(150);
   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto received = std::make_shared<std::promise<std::vector<std::uint8_t>>>();
   auto future = received->get_future();
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    pubsub::topic{.value = "forge.pubsub"},
                    [received](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       received->set_value(event.value.data);
                       co_return pubsub::validation_result::accept;
                    }));

   const auto published = forge::asio::blocking::run(
       runtime, publisher.async_publish(pubsub::topic{.value = "forge.pubsub"},
                                        std::vector<std::uint8_t>{'p', 'u', 'b', 's', 'u', 'b'}));
   BOOST_TEST(!published.signature.empty());

   if (future.wait_for(std::chrono::milliseconds{5'000}) != std::future_status::ready) {
      const auto metrics = subscriber.metrics();
      BOOST_FAIL("pubsub delivery did not finish; received="
                 << metrics.pubsub_messages_received << " delivered=" << metrics.pubsub_messages_delivered
                 << " invalid=" << metrics.pubsub_invalid_messages << " duplicates=" << metrics.pubsub_duplicates
                 << " rejected=" << metrics.protocol_rejections);
   }
   BOOST_TEST(future.get() == std::vector<std::uint8_t>({'p', 'u', 'b', 's', 'u', 'b'}),
              boost::test_tools::per_element());
   BOOST_TEST(publisher.pubsub_snapshot().messages_published >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered >= 1U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_singleflights_connection_and_serializes_first_concurrent_publishes) {
   constexpr auto publish_count = std::size_t{24};
   constexpr auto payload_size = std::size_t{64 * 1024};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 8}};
   const auto publisher_identity = make_test_identity();
   const auto subscriber_identity = make_test_identity();
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   auto publisher = node{runtime, options_for(publisher_identity, pubsub_capabilities)};
   auto subscriber = node{runtime, options_for(subscriber_identity, pubsub_capabilities)};
   const auto subscriber_endpoint = listen_tcp(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   struct delivery_state {
      std::mutex mutex;
      std::condition_variable ready;
      std::set<std::vector<std::uint8_t>> values;
   };
   auto delivered = std::make_shared<delivery_state>();
   const auto subject = pubsub::topic{.value = "forge.pubsub.concurrent"};
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [delivered](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              if (!event.value.data.empty()) {
                 {
                    auto lock = std::scoped_lock{delivered->mutex};
                    delivered->values.insert(event.value.data);
                 }
                 delivered->ready.notify_all();
              }
              co_return pubsub::validation_result::accept;
           }));
   auto publishes = std::vector<std::future<pubsub::message>>{};
   publishes.reserve(publish_count);
   auto ready = std::make_shared<std::atomic_size_t>();
   auto start = std::make_shared<std::atomic_bool>();
   for (auto index = std::size_t{}; index < publish_count; ++index) {
      auto payload = std::vector<std::uint8_t>(payload_size, static_cast<std::uint8_t>(index + 1U));
      publishes.push_back(boost::asio::co_spawn(
          runtime.context(),
          [&publisher, subject, payload = std::move(payload), ready,
           start]() mutable -> boost::asio::awaitable<pubsub::message> {
             ready->fetch_add(1U, std::memory_order_release);
             while (!start->load(std::memory_order_acquire)) {
                co_await boost::asio::post(boost::asio::use_awaitable);
             }
             co_return co_await publisher.async_publish(subject, std::move(payload));
          },
          boost::asio::use_future));
   }
   const auto start_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
   while (ready->load(std::memory_order_acquire) != publish_count &&
          std::chrono::steady_clock::now() < start_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{1}, "concurrent GossipSub start barrier");
   }
   BOOST_REQUIRE_EQUAL(ready->load(std::memory_order_acquire), publish_count);
   start->store(true, std::memory_order_release);
   for (auto& publish : publishes) {
      BOOST_REQUIRE_MESSAGE(publish.wait_for(std::chrono::seconds{10}) == std::future_status::ready,
                            "concurrent GossipSub publish did not finish");
      try {
         static_cast<void>(publish.get());
      } catch (const forge::exceptions::base& error) {
         const auto publisher_metrics = publisher.metrics();
         const auto subscriber_metrics = subscriber.metrics();
         BOOST_FAIL("concurrent GossipSub publish failed: "
                    << error.what() << "; publisher sessions=" << publisher_metrics.active_sessions << " closed="
                    << publisher_metrics.sessions_closed << " pruned=" << publisher_metrics.sessions_pruned
                    << " connection_rejections=" << publisher_metrics.connection_rejections << " direct_failures="
                    << publisher_metrics.direct_failures << " invalid=" << publisher_metrics.pubsub_invalid_messages
                    << " protocol_rejections=" << publisher_metrics.protocol_rejections
                    << " handshakes_failed=" << publisher_metrics.handshakes_failed << "; subscriber sessions="
                    << subscriber_metrics.active_sessions << " closed=" << subscriber_metrics.sessions_closed
                    << " invalid=" << subscriber_metrics.pubsub_invalid_messages
                    << " protocol_rejections=" << subscriber_metrics.protocol_rejections);
      }
   }
   {
      auto lock = std::unique_lock{delivered->mutex};
      BOOST_REQUIRE(delivered->ready.wait_for(lock, std::chrono::seconds{10},
                                              [&] { return delivered->values.size() == publish_count; }));
      for (auto index = std::size_t{}; index < publish_count; ++index) {
         const auto expected = std::vector<std::uint8_t>(payload_size, static_cast<std::uint8_t>(index + 1U));
         BOOST_TEST(delivered->values.contains(expected));
      }
   }
   BOOST_TEST(publisher.metrics().active_sessions == 1U);
   BOOST_TEST(publisher.metrics().sessions_opened == 1U);
   BOOST_TEST(subscriber.metrics().active_sessions == 1U);
   BOOST_TEST(publisher.metrics().pubsub_invalid_messages == 0U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages == 0U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_connection_singleflight_reclaims_transient_peer_gates_after_waiters) {
   constexpr auto peer_count = std::size_t{200};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto registry = detail::connection_singleflight_registry{};
   auto starters = std::vector<detail::connection_singleflight_registry::lease>{};
   auto waiters = std::vector<detail::connection_singleflight_registry::lease>{};
   starters.reserve(peer_count);
   waiters.reserve(peer_count);

   for (auto index = std::size_t{}; index < peer_count; ++index) {
      auto starter = registry.join(peer(static_cast<std::uint8_t>(index + 1U)), runtime.context().get_executor());
      auto waiter = registry.join(peer(static_cast<std::uint8_t>(index + 1U)), runtime.context().get_executor());
      BOOST_REQUIRE(starter.start.has_value());
      BOOST_TEST(!waiter.start.has_value());
      registry.succeed(*starter.start);
      starters.push_back(std::move(starter.participant));
      waiters.push_back(std::move(waiter.participant));
   }
   BOOST_TEST(registry.size() == peer_count);

   for (auto& starter : starters) {
      registry.leave(starter);
   }
   BOOST_TEST(registry.size() == peer_count);

   for (auto& waiter : waiters) {
      const auto result = forge::asio::blocking::run(runtime, waiter.wait());
      BOOST_TEST(result.succeeded);
      registry.leave(waiter);
   }
   BOOST_TEST(registry.size() == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_connection_singleflight_shares_one_typed_failure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto registry = detail::connection_singleflight_registry{};
   const auto remote = peer(217);
   auto starter = registry.join(remote, runtime.context().get_executor());
   auto first_waiter = registry.join(remote, runtime.context().get_executor());
   auto second_waiter = registry.join(remote, runtime.context().get_executor());
   BOOST_REQUIRE(starter.start.has_value());
   BOOST_TEST(!first_waiter.start.has_value());
   BOOST_TEST(!second_waiter.start.has_value());

   registry.fail(*starter.start, exceptions::code::timeout, "shared connection timeout");
   const auto first = forge::asio::blocking::run(runtime, first_waiter.participant.wait());
   const auto second = forge::asio::blocking::run(runtime, second_waiter.participant.wait());
   BOOST_TEST(!first.succeeded);
   BOOST_TEST(!second.succeeded);
   BOOST_REQUIRE(first.error.has_value());
   BOOST_REQUIRE(second.error.has_value());
   BOOST_CHECK(*first.error == exceptions::code::timeout);
   BOOST_CHECK(*second.error == exceptions::code::timeout);
   BOOST_TEST(first.message == "shared connection timeout");
   BOOST_TEST(second.message == first.message);

   registry.leave(starter.participant);
   registry.leave(first_waiter.participant);
   registry.leave(second_waiter.participant);
   BOOST_TEST(registry.size() == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_connection_singleflight_survives_starter_cancellation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto registry = detail::connection_singleflight_registry{};
   const auto remote = peer(216);
   auto starter = registry.join(remote, runtime.context().get_executor());
   auto waiter = registry.join(remote, runtime.context().get_executor());
   BOOST_REQUIRE(starter.start.has_value());
   BOOST_TEST(!waiter.start.has_value());

   registry.leave(starter.participant);
   BOOST_TEST(registry.size() == 1U);
   registry.succeed(*starter.start);
   const auto result = forge::asio::blocking::run(runtime, waiter.participant.wait());
   BOOST_TEST(result.succeeded);
   registry.leave(waiter.participant);
   BOOST_TEST(registry.size() == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_non_owner_session_close_keeps_cached_stream_generation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_certificate_identity("pubsub-generation-publisher");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-generation-subscriber");
   const auto pressure_identity = make_test_certificate_identity("pubsub-generation-pressure");
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   auto publisher_options = options_for(publisher_identity, pubsub_capabilities);
   publisher_options.limits.max_sessions = 2;
   publisher_options.limits.max_outbound_sessions = 2;
   publisher_options.limits.max_sessions_per_peer = 2;
   publisher_options.limits.session_low_watermark = 1;
   publisher_options.limits.session_grace_period = std::chrono::milliseconds{0};
   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, options_for(subscriber_identity, pubsub_capabilities)};
   auto pressure = node{runtime, options_for(pressure_identity)};
   const auto publisher_endpoint = listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   const auto pressure_endpoint = listen(pressure, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint, pubsub_capabilities);

   struct delivery_state {
      std::mutex mutex;
      std::condition_variable ready;
      std::size_t count = 0;
   };
   auto delivered = std::make_shared<delivery_state>();
   const auto subject = pubsub::topic{.value = "forge.pubsub.session-generation"};
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    subject, [delivered](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
                       {
                          auto lock = std::scoped_lock{delivered->mutex};
                          ++delivered->count;
                       }
                       delivered->ready.notify_all();
                       co_return pubsub::validation_result::accept;
                    }));
   forge::asio::blocking::run(
       runtime,
       subscriber.async_connect(publisher_endpoint, node::connect_options{.expected_peer = publisher.local_peer()}));
   forge::asio::blocking::run(
       runtime,
       publisher.async_connect(subscriber_endpoint, node::connect_options{.expected_peer = subscriber.local_peer()}));
   const auto session_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
   while (publisher.diagnostics().sessions.size() != 2U && std::chrono::steady_clock::now() < session_deadline) {
      wait_on_runtime(runtime, std::chrono::milliseconds{1}, "publisher inbound session barrier");
   }
   const auto before = publisher.diagnostics();
   BOOST_REQUIRE_EQUAL(before.sessions.size(), 2U);
   BOOST_TEST(before.sessions[0].remote_peer.to_string() == subscriber.local_peer().to_string());
   BOOST_TEST(before.sessions[1].remote_peer.to_string() == subscriber.local_peer().to_string());
   const auto non_owner_session_id = before.sessions[0].id;
   const auto owner_session_id = before.sessions[1].id;

   forge::asio::blocking::run(runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{0x41U}));
   {
      auto lock = std::unique_lock{delivered->mutex};
      BOOST_REQUIRE(delivered->ready.wait_for(lock, std::chrono::seconds{5}, [&] { return delivered->count == 1U; }));
   }
   const auto opened_streams = publisher.metrics().protocol_streams_opened;
   BOOST_REQUIRE(opened_streams >= 1U);

   forge::asio::blocking::run(
       runtime,
       publisher.async_connect(pressure_endpoint, node::connect_options{.expected_peer = pressure.local_peer()}));
   const auto after_prune = publisher.diagnostics();
   BOOST_REQUIRE_EQUAL(after_prune.sessions.size(), 2U);
   BOOST_TEST(publisher.metrics().sessions_pruned >= 1U);
   BOOST_TEST(std::ranges::none_of(after_prune.sessions,
                                   [&](const auto& session) { return session.id == non_owner_session_id; }));
   BOOST_TEST(
       std::ranges::any_of(after_prune.sessions, [&](const auto& session) { return session.id == owner_session_id; }));
   BOOST_TEST(publisher.metrics().pubsub_invalid_messages == 0U);

   forge::asio::blocking::run(runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{0x42U}));
   {
      auto lock = std::unique_lock{delivered->mutex};
      BOOST_REQUIRE(delivered->ready.wait_for(lock, std::chrono::seconds{5}, [&] { return delivered->count == 2U; }));
   }
   BOOST_TEST(publisher.metrics().protocol_streams_opened == opened_streams);
   BOOST_TEST(publisher.metrics().pubsub_invalid_messages == 0U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
   forge::asio::blocking::run(runtime, pressure.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_dead_outbound_generation_detaches_mesh_and_reannounces_before_graft) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_certificate_identity("pubsub-outbound-generation-publisher");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-outbound-generation-subscriber");
   auto publisher_options = pubsub_options_for(publisher_identity);
   publisher_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   auto subscriber_options = pubsub_options_for(subscriber_identity);
   subscriber_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   subscriber_options.limits.pubsub.limits.max_data_size = 4;

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.outbound-generation"};
   const auto accept = [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
      co_return pubsub::validation_result::accept;
   };
   forge::asio::blocking::run(runtime, publisher.async_subscribe(subject, accept));
   forge::asio::blocking::run(runtime, subscriber.async_subscribe(subject, accept));
   forge::asio::blocking::run(
       runtime,
       publisher.async_connect(subscriber_endpoint, node::connect_options{.expected_peer = subscriber.local_peer()}));

   auto inbound = forge::asio::blocking::run(
       runtime, subscriber.async_open_protocol_stream(publisher.local_peer(), builtins::meshsub_v11));
   const auto subscribe_and_graft = pubsub::codec::encode(pubsub::rpc{
       .subscriptions = {pubsub::subscription{.subscribe = true, .subject = subject}},
       .control_value = pubsub::control{.grafts = {pubsub::control::graft{.subject = subject}}},
   });
   forge::asio::blocking::run(runtime, inbound.async_write(subscribe_and_graft));
   for (auto poll = 0;
        poll < 200 && (publisher.pubsub_snapshot().peers != 1U || publisher.pubsub_snapshot().mesh_edges != 1U);
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub explicit inbound generation");
   }
   BOOST_REQUIRE(publisher.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(publisher.pubsub_snapshot().mesh_edges == 1U);
   const auto active_sessions = publisher.metrics().active_sessions;
   BOOST_REQUIRE(active_sessions > 0U);

   forge::asio::blocking::run(runtime,
                              publisher.async_publish(subject, std::vector<std::uint8_t>(8, std::uint8_t{0x41U})));
   for (auto poll = 0; poll < 200 && subscriber.metrics().protocol_rejections < 1U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub G1 remote reset");
   }
   BOOST_REQUIRE(subscriber.metrics().protocol_rejections >= 1U);
   const auto g1_streams = publisher.metrics().protocol_streams_opened;

   for (auto attempt = 0; attempt < 50 && publisher.metrics().protocol_streams_opened <= g1_streams; ++attempt) {
      try {
         static_cast<void>(forge::asio::blocking::run(
             runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{static_cast<std::uint8_t>(attempt)})));
      } catch (const forge::exceptions::base&) {
      }
      if (publisher.metrics().protocol_streams_opened <= g1_streams) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub G1 reset propagation");
      }
   }
   BOOST_REQUIRE(publisher.metrics().protocol_streams_opened > g1_streams);
   BOOST_REQUIRE(publisher.pubsub_snapshot().mesh_edges == 0U);
   BOOST_REQUIRE(publisher.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(subscriber.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(publisher.metrics().active_sessions == active_sessions);
   const auto g2_streams = publisher.metrics().protocol_streams_opened;

   forge::asio::blocking::run(runtime,
                              publisher.async_publish(subject, std::vector<std::uint8_t>(8, std::uint8_t{0x42U})));
   for (auto poll = 0; poll < 400 && subscriber.metrics().protocol_rejections < 2U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub G2 remote reset");
   }
   BOOST_REQUIRE(subscriber.metrics().protocol_rejections >= 2U);
   for (auto attempt = 0; attempt < 50 && publisher.metrics().protocol_streams_opened <= g2_streams; ++attempt) {
      try {
         static_cast<void>(forge::asio::blocking::run(
             runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{static_cast<std::uint8_t>(attempt)})));
      } catch (const forge::exceptions::base&) {
      }
      if (publisher.metrics().protocol_streams_opened <= g2_streams) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub failed G2 cleanup");
      }
   }
   BOOST_REQUIRE(publisher.metrics().protocol_streams_opened > g2_streams);
   BOOST_REQUIRE(publisher.pubsub_snapshot().mesh_edges == 0U);
   BOOST_REQUIRE(publisher.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(publisher.metrics().active_sessions == active_sessions);

   forge::asio::blocking::run(runtime, inbound.async_close());
   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_rejects_publish_after_cached_stream_shutdown) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_identity();
   const auto subscriber_identity = make_test_identity();
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   auto publisher = node{runtime, options_for(publisher_identity, pubsub_capabilities)};
   auto subscriber = node{runtime, options_for(subscriber_identity, pubsub_capabilities)};
   const auto subscriber_endpoint = listen_tcp(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint, pubsub_capabilities);

   const auto subject = pubsub::topic{.value = "forge.pubsub.shutdown"};
   auto delivered = std::make_shared<std::promise<void>>();
   auto delivery = delivered->get_future();
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    subject, [delivered](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       delivered->set_value();
                       co_return pubsub::validation_result::accept;
                    }));
   forge::asio::blocking::run(
       runtime,
       publisher.async_connect(subscriber_endpoint, node::connect_options{.expected_peer = subscriber.local_peer()}));
   forge::asio::blocking::run(runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{0x42U}));
   BOOST_REQUIRE(delivery.wait_for(std::chrono::seconds{5}) == std::future_status::ready);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   BOOST_TEST(publisher.metrics().stopped);
   try {
      static_cast<void>(
          forge::asio::blocking::run(runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{0x43U})));
      BOOST_FAIL("GossipSub publish should reject after clean shutdown");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::closed));
   }

   const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (subscriber.metrics().active_sessions != 0U) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < close_deadline);
      wait_on_runtime(runtime, std::chrono::milliseconds{1}, "orderly GossipSub session close");
   }
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages == 0U);

   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_separates_immediate_source_from_signed_author) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto author_identity = make_test_certificate_identity("pubsub-signed-author");
   const auto relay_identity = make_test_certificate_identity("pubsub-immediate-relay");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-source-subscriber");
   auto author = node{runtime, pubsub_options_for(author_identity)};
   auto relay = node{runtime, pubsub_options_for(relay_identity)};
   auto subscriber = node{runtime, pubsub_options_for(subscriber_identity)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   relay.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.source-author"};
   auto delivered = std::make_shared<std::promise<std::pair<peer_id, std::optional<peer_id>>>>();
   auto future = delivered->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [delivered](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              delivered->set_value({event.source, event.value.from});
              co_return pubsub::validation_result::accept;
           }));

   const auto published =
       forge::asio::blocking::run(runtime, author.async_publish(subject, std::vector<std::uint8_t>{1, 2, 3}));
   BOOST_REQUIRE(published.from.has_value());
   BOOST_TEST(published.from->to_string() == author.local_peer().to_string());
   auto relay_stream = forge::asio::blocking::run(
       runtime, relay.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(runtime,
                              relay_stream.async_write(pubsub::codec::encode(pubsub::rpc{.messages = {published}})));

   BOOST_REQUIRE(future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   const auto [source, signed_author] = future.get();
   BOOST_TEST(source.to_string() == relay.local_peer().to_string());
   BOOST_REQUIRE(signed_author.has_value());
   BOOST_TEST(signed_author->to_string() == author.local_peer().to_string());

   forge::asio::blocking::run(runtime, relay_stream.async_close());
   forge::asio::blocking::run(runtime, author.async_stop());
   forge::asio::blocking::run(runtime, relay.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_retry_is_redelivered_after_bounded_cooldown) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   publisher_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.explicit_peer_id = peer(153);
   for (auto* options : {&publisher_options, &subscriber_options}) {
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
      options->limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{40};
      options->limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{80};
      options->limits.pubsub.limits.history_gossip = 1;
   }

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   (void)listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.retry"};
   forge::asio::blocking::run(
       runtime,
       publisher.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto first_attempt = std::make_shared<std::promise<void>>();
   auto first_attempt_future = first_attempt->get_future();
   auto accepted = std::make_shared<std::promise<void>>();
   auto accepted_future = accepted->get_future();
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    subject,
                    [attempts, first_attempt,
                     accepted](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       const auto attempt = attempts->fetch_add(1, std::memory_order_relaxed);
                       if (attempt == 0) {
                          first_attempt->set_value();
                          auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
                          timer.expires_after(std::chrono::milliseconds{25});
                          boost::system::error_code ec;
                          co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
                          co_return pubsub::validation_result::retry;
                       }
                       if (attempt == 1) {
                          accepted->set_value();
                       }
                       co_return pubsub::validation_result::accept;
                    }));

   (void)forge::asio::blocking::run(runtime,
                                    publisher.async_publish(subject, std::vector<std::uint8_t>{'r', 'e', 't', 'r', 'y'},
                                                            pubsub::publish_options{.sign = false}));
   BOOST_REQUIRE(first_attempt_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   first_attempt_future.get();
   const auto noise_subject = pubsub::topic{.value = "forge.pubsub.retry.noise"};
   for (auto value = std::uint8_t{}; value < 8; ++value) {
      (void)forge::asio::blocking::run(runtime, publisher.async_publish(noise_subject, std::vector<std::uint8_t>{value},
                                                                        pubsub::publish_options{.sign = false}));
   }

   if (accepted_future.wait_for(std::chrono::seconds{5}) != std::future_status::ready) {
      const auto snapshot = subscriber.pubsub_snapshot();
      BOOST_FAIL("retryable PubSub message was not redelivered; attempts="
                 << attempts->load(std::memory_order_relaxed) << " received=" << snapshot.messages_received
                 << " delivered=" << snapshot.messages_delivered << " duplicates=" << snapshot.duplicates
                 << " controls=" << snapshot.control_messages);
   }
   accepted_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{50}, "post-redelivery accounting");

   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 2U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 1U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_ignore_remains_terminal_during_history_window) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   publisher_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   for (auto* options : {&publisher_options, &subscriber_options}) {
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
      options->limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{40};
   }

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   (void)listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.ignore"};
   forge::asio::blocking::run(
       runtime,
       publisher.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));
   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto ignored = std::make_shared<std::promise<void>>();
   auto ignored_future = ignored->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [attempts, ignored](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              if (attempts->fetch_add(1, std::memory_order_relaxed) == 0) {
                 ignored->set_value();
              }
              co_return pubsub::validation_result::ignore;
           }));

   (void)forge::asio::blocking::run(
       runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{'i', 'g', 'n', 'o', 'r', 'e'},
                                        pubsub::publish_options{.sign = false}));
   BOOST_REQUIRE(ignored_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   ignored_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{250}, "terminal ignore window");

   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 0U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_retry_rejects_signed_equivocation_for_same_message_id) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_certificate_identity("pubsub-retry-equivocation-publisher");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-retry-equivocation-subscriber");
   auto subscriber_options = pubsub_options_for(subscriber_identity);
   subscriber_options.limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{40};
   subscriber_options.limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{40};
   auto publisher_options = pubsub_options_for(publisher_identity);

   auto subscriber = node{runtime, std::move(subscriber_options)};
   auto publisher = node{runtime, std::move(publisher_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.retry.equivocation"};
   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto ignored = std::make_shared<std::promise<void>>();
   auto ignored_future = ignored->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [attempts, ignored](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              if (attempts->fetch_add(1, std::memory_order_relaxed) == 0) {
                 ignored->set_value();
              }
              co_return pubsub::validation_result::retry;
           }));

   auto first = pubsub::message{
       .data = std::vector<std::uint8_t>{'f', 'i', 'r', 's', 't'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 17},
       .subject = subject,
   };
   const auto private_key = forge::crypto::pki::pem::read_private_key(publisher_identity.private_key_pem);
   pubsub::codec::sign_message(first, private_key);
   auto second = first;
   second.data = std::vector<std::uint8_t>{'s', 'e', 'c', 'o', 'n', 'd'};
   pubsub::codec::sign_message(second, private_key);
   BOOST_REQUIRE(pubsub::codec::verify_message(first));
   BOOST_REQUIRE(pubsub::codec::verify_message(second));
   BOOST_TEST(pubsub::codec::message_id(first) == pubsub::codec::message_id(second), boost::test_tools::per_element());

   auto stream = forge::asio::blocking::run(
       runtime, publisher.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{first}})));
   BOOST_REQUIRE(ignored_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   ignored_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{60}, "signed equivocation retry cooldown");

   forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{second}})));
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "signed equivocation accounting");

   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 1U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages >= 1U);
   BOOST_TEST(subscriber.metrics().protocol_rejections >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 0U);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_retry_accepts_equivalent_signed_envelope_without_inline_key) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_certificate_identity("pubsub-retry-envelope-publisher");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-retry-envelope-subscriber");
   auto publisher_options = pubsub_options_for(publisher_identity);
   auto subscriber_options = pubsub_options_for(subscriber_identity);
   subscriber_options.limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{40};
   subscriber_options.limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{40};

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.retry.envelope"};
   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto retryable = std::make_shared<std::promise<void>>();
   auto retryable_future = retryable->get_future();
   auto accepted = std::make_shared<std::promise<void>>();
   auto accepted_future = accepted->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject,
           [attempts, retryable, accepted](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              const auto attempt = attempts->fetch_add(1, std::memory_order_relaxed);
              if (attempt == 0) {
                 retryable->set_value();
                 co_return pubsub::validation_result::retry;
              }
              accepted->set_value();
              co_return pubsub::validation_result::accept;
           }));

   const auto author = make_test_identity();
   auto with_key = pubsub::message{
       .data = std::vector<std::uint8_t>{'e', 'n', 'v', 'e', 'l', 'o', 'p', 'e'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 18},
       .subject = subject,
   };
   pubsub::codec::sign_message(with_key, author.private_key);
   auto without_key = with_key;
   without_key.key.clear();
   BOOST_REQUIRE(pubsub::codec::verify_message(with_key));
   BOOST_REQUIRE(pubsub::codec::verify_message(without_key));

   auto stream = forge::asio::blocking::run(
       runtime, publisher.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{with_key}})));
   BOOST_REQUIRE(retryable_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   retryable_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{60}, "equivalent envelope retry cooldown");
   forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{without_key}})));

   BOOST_REQUIRE(accepted_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   accepted_future.get();
   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 2U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages == 0U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 1U);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_in_progress_validation_is_not_replaced_after_cache_pressure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   const auto first_transport_identity = make_test_certificate_identity("pubsub-validation-first-transport");
   const auto competing_transport_identity = make_test_certificate_identity("pubsub-validation-competing-transport");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-validation-pressure-subscriber");
   auto first_options = pubsub_options_for(first_transport_identity);
   auto competing_options = pubsub_options_for(competing_transport_identity);
   auto subscriber_options = pubsub_options_for(subscriber_identity);
   subscriber_options.limits.pubsub.limits.history_length = 1;
   subscriber_options.limits.pubsub.limits.max_messages = 1;

   auto first_transport = node{runtime, std::move(first_options)};
   auto competing_transport = node{runtime, std::move(competing_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   first_transport.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                          capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   competing_transport.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.validation.pressure"};
   auto first_entered = std::make_shared<std::promise<void>>();
   auto first_entered_future = first_entered->get_future();
   auto second_payloads = std::make_shared<std::atomic_uint64_t>(0);
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    subject,
                    [first_entered, second_payloads](
                        pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       if (event.value.data == std::vector<std::uint8_t>{'f', 'i', 'r', 's', 't'}) {
                          first_entered->set_value();
                          auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
                          timer.expires_after(std::chrono::milliseconds{300});
                          co_await timer.async_wait(boost::asio::use_awaitable);
                       } else if (event.value.data == std::vector<std::uint8_t>{'s', 'e', 'c', 'o', 'n', 'd'}) {
                          second_payloads->fetch_add(1, std::memory_order_relaxed);
                       }
                       co_return pubsub::validation_result::accept;
                    }));

   const auto author = make_test_identity();
   auto first = pubsub::message{
       .data = std::vector<std::uint8_t>{'f', 'i', 'r', 's', 't'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 19},
       .subject = subject,
   };
   pubsub::codec::sign_message(first, author.private_key);
   auto second = first;
   second.data = std::vector<std::uint8_t>{'s', 'e', 'c', 'o', 'n', 'd'};
   pubsub::codec::sign_message(second, author.private_key);
   auto filler = pubsub::message{
       .data = std::vector<std::uint8_t>{'f', 'i', 'l', 'l', 'e', 'r'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 20},
       .subject = subject,
   };
   pubsub::codec::sign_message(filler, author.private_key);

   auto first_stream = forge::asio::blocking::run(
       runtime, first_transport.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   auto competing_stream = forge::asio::blocking::run(
       runtime, competing_transport.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(runtime, first_stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{first}})));
   BOOST_REQUIRE(first_entered_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   first_entered_future.get();
   forge::asio::blocking::run(runtime, competing_stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{filler}})));
   forge::asio::blocking::run(runtime, competing_stream.async_write(pubsub::codec::encode(
                                           pubsub::rpc{.messages = std::vector<pubsub::message>{second}})));
   wait_on_runtime(runtime, std::chrono::milliseconds{500}, "in-progress validation completion");

   BOOST_TEST(second_payloads->load(std::memory_order_relaxed) == 0U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 2U);

   forge::asio::blocking::run(runtime, first_stream.async_close());
   forge::asio::blocking::run(runtime, competing_stream.async_close());
   forge::asio::blocking::run(runtime, first_transport.async_stop());
   forge::asio::blocking::run(runtime, competing_transport.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_proactive_redelivery_stops_after_backpressure_budget) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   const auto occupying_identity = make_test_certificate_identity("pubsub-redelivery-occupying");
   const auto retrying_identity = make_test_certificate_identity("pubsub-redelivery-retrying");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-redelivery-subscriber");
   auto occupying_options = pubsub_options_for(occupying_identity);
   auto retrying_options = pubsub_options_for(retrying_identity);
   auto subscriber_options = pubsub_options_for(subscriber_identity);
   subscriber_options.limits.pubsub.limits.max_validation_queue = 1;
   subscriber_options.limits.pubsub.limits.max_validation_redeliveries = 2;
   subscriber_options.limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{20};
   subscriber_options.limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{20};

   auto occupying = node{runtime, std::move(occupying_options)};
   auto retrying = node{runtime, std::move(retrying_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   occupying.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   retrying.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                   capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.redelivery.budget"};
   auto occupying_entered = std::make_shared<std::promise<void>>();
   auto occupying_entered_future = occupying_entered->get_future();
   auto retrying_callbacks = std::make_shared<std::atomic_uint64_t>(0);
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    subject,
                    [occupying_entered, retrying_callbacks](
                        pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       if (event.value.data == std::vector<std::uint8_t>{'o', 'c', 'c', 'u', 'p', 'y'}) {
                          occupying_entered->set_value();
                          auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
                          timer.expires_after(std::chrono::milliseconds{400});
                          co_await timer.async_wait(boost::asio::use_awaitable);
                       } else {
                          retrying_callbacks->fetch_add(1, std::memory_order_relaxed);
                       }
                       co_return pubsub::validation_result::accept;
                    }));

   const auto author = make_test_identity();
   auto occupying_message = pubsub::message{
       .data = std::vector<std::uint8_t>{'o', 'c', 'c', 'u', 'p', 'y'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 21},
       .subject = subject,
   };
   pubsub::codec::sign_message(occupying_message, author.private_key);
   auto retrying_message = pubsub::message{
       .data = std::vector<std::uint8_t>{'r', 'e', 't', 'r', 'y'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 22},
       .subject = subject,
   };
   pubsub::codec::sign_message(retrying_message, author.private_key);

   auto occupying_stream = forge::asio::blocking::run(
       runtime, occupying.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   auto retrying_stream = forge::asio::blocking::run(
       runtime, retrying.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(
       runtime, occupying_stream.async_write(pubsub::codec::encode(pubsub::rpc{.messages = {occupying_message}})));
   BOOST_REQUIRE(occupying_entered_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   occupying_entered_future.get();

   for (auto delivery = 0; delivery < 4; ++delivery) {
      forge::asio::blocking::run(
          runtime, retrying_stream.async_write(pubsub::codec::encode(pubsub::rpc{.messages = {retrying_message}})));
      wait_on_runtime(runtime, std::chrono::milliseconds{40}, "proactive redelivery");
   }
   wait_on_runtime(runtime, std::chrono::milliseconds{450}, "validation queue release");
   forge::asio::blocking::run(
       runtime, retrying_stream.async_write(pubsub::codec::encode(pubsub::rpc{.messages = {retrying_message}})));
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "post-redelivery-budget delivery");

   BOOST_TEST(retrying_callbacks->load(std::memory_order_relaxed) == 0U);
   BOOST_TEST(subscriber.metrics().backpressure_rejections == 3U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages == 0U);

   forge::asio::blocking::run(runtime, occupying_stream.async_close());
   forge::asio::blocking::run(runtime, retrying_stream.async_close());
   forge::asio::blocking::run(runtime, occupying.async_stop());
   forge::asio::blocking::run(runtime, retrying.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_retry_source_is_not_replaced_by_foreign_ihave) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   const auto publisher_identity = make_test_certificate_identity("pubsub-retry-source-publisher");
   const auto subscriber_identity = make_test_certificate_identity("pubsub-retry-source-subscriber");
   const auto attacker_identity = make_test_certificate_identity("pubsub-retry-source-attacker");
   auto publisher_options = pubsub_options_for(publisher_identity);
   auto subscriber_options = pubsub_options_for(subscriber_identity);
   auto attacker_options = pubsub_options_for(attacker_identity);
   publisher_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   attacker_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{500};
   subscriber_options.limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{100};
   subscriber_options.limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{100};
   subscriber_options.limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{100};
   subscriber_options.limits.pubsub.limits.max_validation_requests = 2;

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   auto attacker = node{runtime, std::move(attacker_options)};
   (void)listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   (void)listen(attacker, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   attacker.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                   capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.retry.source"};
   forge::asio::blocking::run(
       runtime,
       publisher.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto ignored = std::make_shared<std::promise<void>>();
   auto ignored_future = ignored->get_future();
   auto accepted = std::make_shared<std::promise<peer_id>>();
   auto accepted_future = accepted->get_future();
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(subject,
                                           [attempts, ignored, accepted](pubsub::event event) mutable
                                               -> boost::asio::awaitable<pubsub::validation_result> {
                                              const auto attempt = attempts->fetch_add(1, std::memory_order_relaxed);
                                              if (attempt == 0) {
                                                 ignored->set_value();
                                                 co_return pubsub::validation_result::retry;
                                              }
                                              if (attempt == 1) {
                                                 accepted->set_value(event.source);
                                              }
                                              co_return pubsub::validation_result::accept;
                                           }));

   const auto published = forge::asio::blocking::run(
       runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{'s', 'o', 'u', 'r', 'c', 'e'},
                                        pubsub::publish_options{.sign = false}));
   BOOST_REQUIRE(ignored_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   ignored_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{120}, "foreign IHAVE injection window");

   auto attacker_stream = forge::asio::blocking::run(
       runtime, attacker.async_open_protocol_stream(subscriber.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(
       runtime,
       attacker_stream.async_write(pubsub::codec::encode(pubsub::rpc{
           .control_value =
               pubsub::control{
                   .have = std::vector<pubsub::control::ihave>{pubsub::control::ihave{
                       .subject = subject,
                       .message_ids = std::vector<std::vector<std::uint8_t>>{pubsub::codec::message_id(published)}}},
               },
       })));

   BOOST_REQUIRE(accepted_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   BOOST_TEST(accepted_future.get().to_string() == publisher.local_peer().to_string());
   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 2U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 1U);

   forge::asio::blocking::run(runtime, attacker_stream.async_close());
   forge::asio::blocking::run(runtime, attacker.async_stop());
   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_retry_stops_after_max_attempts_without_duplicate_history) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   publisher_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.explicit_peer_id = peer(155);
   for (auto* options : {&publisher_options, &subscriber_options}) {
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
      options->limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{40};
      options->limits.pubsub.limits.max_validation_attempts = 3;
   }
   subscriber_options.limits.pubsub.limits.history_length = 1;
   subscriber_options.limits.pubsub.limits.max_messages = 1;

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   (void)listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.retry.limit"};
   forge::asio::blocking::run(
       runtime,
       publisher.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto exhausted = std::make_shared<std::promise<void>>();
   auto exhausted_future = exhausted->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [attempts, exhausted](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              if (attempts->fetch_add(1, std::memory_order_relaxed) == 2) {
                 exhausted->set_value();
              }
              co_return pubsub::validation_result::retry;
           }));

   (void)forge::asio::blocking::run(runtime,
                                    publisher.async_publish(subject, std::vector<std::uint8_t>{'l', 'i', 'm', 'i', 't'},
                                                            pubsub::publish_options{.sign = false}));
   BOOST_REQUIRE(exhausted_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   exhausted_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{250}, "terminal retry limit window");

   const auto snapshot = subscriber.pubsub_snapshot();
   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 3U);
   BOOST_TEST(snapshot.messages_delivered == 0U);
   BOOST_TEST(snapshot.cached_messages == 1U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_unavailable_source_stops_retry_requests_after_limit) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   publisher_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.explicit_peer_id = peer(156);
   for (auto* options : {&publisher_options, &subscriber_options}) {
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
      options->limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{100};
      options->limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{100};
   }
   publisher_options.limits.pubsub.limits.history_length = 1;
   publisher_options.limits.pubsub.limits.max_messages = 1;
   subscriber_options.limits.pubsub.limits.max_validation_requests = 2;

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   (void)listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.retry.unavailable"};
   forge::asio::blocking::run(
       runtime,
       publisher.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto ignored = std::make_shared<std::promise<void>>();
   auto ignored_future = ignored->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [attempts, ignored](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              if (attempts->fetch_add(1, std::memory_order_relaxed) == 0) {
                 ignored->set_value();
              }
              co_return pubsub::validation_result::retry;
           }));

   (void)forge::asio::blocking::run(
       runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{'u', 'n', 'a', 'v', 'a', 'i', 'l'},
                                        pubsub::publish_options{.sign = false}));
   BOOST_REQUIRE(ignored_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   ignored_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{25}, "initial ignore completion");

   const auto noise_subject = pubsub::topic{.value = "forge.pubsub.retry.eviction"};
   (void)forge::asio::blocking::run(runtime, publisher.async_publish(noise_subject, std::vector<std::uint8_t>{0xff},
                                                                     pubsub::publish_options{.sign = false}));
   const auto control_before = publisher.pubsub_snapshot().control_messages;
   for (auto poll = 0; poll < 100 && publisher.pubsub_snapshot().control_messages < control_before + 2; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{20}, "bounded unavailable-source retry");
   }
   const auto control_at_limit = publisher.pubsub_snapshot().control_messages;
   BOOST_REQUIRE(control_at_limit >= control_before + 2);

   wait_on_runtime(runtime, std::chrono::milliseconds{350}, "post-request-limit window");
   BOOST_TEST(publisher.pubsub_snapshot().control_messages == control_at_limit);
   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 1U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_reject_remains_terminal_during_history_window) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   publisher_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   subscriber_options.explicit_peer_id = peer(154);
   for (auto* options : {&publisher_options, &subscriber_options}) {
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
      options->limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
      options->limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{30};
      options->limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{60};
   }

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   (void)listen(publisher, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.pubsub.reject"};
   forge::asio::blocking::run(
       runtime,
       publisher.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   auto attempts = std::make_shared<std::atomic_uint64_t>(0);
   auto rejected = std::make_shared<std::promise<void>>();
   auto rejected_future = rejected->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [attempts, rejected](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              const auto attempt = attempts->fetch_add(1, std::memory_order_relaxed);
              if (attempt == 0) {
                 rejected->set_value();
              }
              co_return pubsub::validation_result::reject;
           }));

   (void)forge::asio::blocking::run(
       runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{'r', 'e', 'j', 'e', 'c', 't'},
                                        pubsub::publish_options{.sign = false}));
   BOOST_REQUIRE(rejected_future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
   rejected_future.get();
   wait_on_runtime(runtime, std::chrono::milliseconds{250}, "terminal rejection gossip window");

   BOOST_TEST(attempts->load(std::memory_order_relaxed) == 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 0U);
   BOOST_TEST(subscriber.pubsub_snapshot().invalid_messages >= 1U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_forwards_between_subscribed_peers) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   auto publisher_options = pubsub_options_for();
   auto hub_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   hub_options.explicit_peer_id = peer(151);
   subscriber_options.explicit_peer_id = peer(152);

   auto publisher = node{runtime, std::move(publisher_options)};
   auto hub = node{runtime, std::move(hub_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto hub_endpoint = listen(hub, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);

   publisher.peers().learn_endpoint(hub.local_peer(), hub_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   subscriber.peers().learn_endpoint(hub.local_peer(), hub_endpoint,
                                     capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   hub.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.mesh"};
   forge::asio::blocking::run(
       runtime, hub.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));
   auto received = std::make_shared<std::promise<std::vector<std::uint8_t>>>();
   auto future = received->get_future();
   forge::asio::blocking::run(
       runtime,
       subscriber.async_subscribe(
           subject, [received](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
              received->set_value(event.value.data);
              co_return pubsub::validation_result::accept;
           }));
   for (auto poll = 0; poll < 200 && hub.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub heartbeat GRAFT");
   }
   BOOST_REQUIRE(hub.pubsub_snapshot().mesh_edges >= 1U);

   forge::asio::blocking::run(runtime, publisher.async_publish(subject, std::vector<std::uint8_t>{'m', 'e', 's', 'h'}));

   if (future.wait_for(std::chrono::milliseconds{5'000}) != std::future_status::ready) {
      const auto hub_metrics = hub.metrics();
      const auto subscriber_metrics = subscriber.metrics();
      BOOST_FAIL("pubsub forwarding did not finish; hub_received="
                 << hub_metrics.pubsub_messages_received << " hub_delivered=" << hub_metrics.pubsub_messages_delivered
                 << " subscriber_received=" << subscriber_metrics.pubsub_messages_received
                 << " subscriber_delivered=" << subscriber_metrics.pubsub_messages_delivered
                 << " subscriber_invalid=" << subscriber_metrics.pubsub_invalid_messages);
   }
   BOOST_TEST(future.get() == std::vector<std::uint8_t>({'m', 'e', 's', 'h'}), boost::test_tools::per_element());
   BOOST_TEST(hub.pubsub_snapshot().mesh_edges >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered >= 1U);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, hub.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_subscription_is_idempotent_and_does_not_create_mesh_edge) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.resources.max_streams_per_protocol = 1;
   server_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   auto client_options = pubsub_options_for();
   client_options.explicit_peer_id = peer(219);

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto subject = pubsub::topic{.value = "forge.subscription.idempotent"};
   forge::asio::blocking::run(
       runtime, server.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));
   const auto denied_before = server.diagnostics().resources.denied_streams;

   const auto subscribe = pubsub::codec::encode(pubsub::rpc{
       .subscriptions =
           std::vector<pubsub::subscription>{
               pubsub::subscription{.subscribe = true, .subject = subject},
           },
   });
   forge::asio::blocking::run(runtime, stream.async_write(subscribe));
   forge::asio::blocking::run(runtime, stream.async_write(subscribe));
   for (auto poll = 0; poll < 100 && server.pubsub_snapshot().peers == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub subscription processing");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);
   BOOST_TEST(server.diagnostics().resources.denied_streams == denied_before);

   const auto graft = pubsub::codec::encode(pubsub::rpc{
       .control_value =
           pubsub::control{
               .grafts =
                   std::vector<pubsub::control::graft>{
                       pubsub::control::graft{.subject = subject},
                   },
           },
   });
   forge::asio::blocking::run(runtime, stream.async_write(graft));
   for (auto poll = 0; poll < 100 && server.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub GRAFT processing");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);

   forge::asio::blocking::run(runtime, stream.async_write(graft));
   wait_on_runtime(runtime, std::chrono::milliseconds{25}, "duplicate GossipSub GRAFT processing");
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 1U);

   const auto unsubscribe = pubsub::codec::encode(pubsub::rpc{
       .subscriptions =
           std::vector<pubsub::subscription>{
               pubsub::subscription{.subscribe = false, .subject = subject},
           },
   });
   forge::asio::blocking::run(runtime, stream.async_write(unsubscribe));
   for (auto poll = 0; poll < 100 && server.pubsub_snapshot().mesh_edges != 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub unsubscribe processing");
   }
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);
   forge::asio::blocking::run(runtime, stream.async_write(unsubscribe));
   wait_on_runtime(runtime, std::chrono::milliseconds{25}, "duplicate GossipSub unsubscribe processing");
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_graft_at_topic_subscription_cap_sends_prune_without_state_or_subscribe_loop) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   auto server_options = pubsub_options_for(make_test_certificate_identity("graft-capacity-server"));
   server_options.limits.pubsub.limits.max_peers_per_topic = 1;
   server_options.limits.pubsub.limits.prune_backoff = std::chrono::seconds{1};
   server_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   server_options.limits.resources.max_malformed_messages_per_peer = 1;
   auto occupying_options = pubsub_options_for(make_test_certificate_identity("graft-capacity-occupying"));
   occupying_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   auto rejected_options = pubsub_options_for(make_test_certificate_identity("graft-capacity-rejected"));
   rejected_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};

   auto server = node{runtime, std::move(server_options)};
   auto occupying = node{runtime, std::move(occupying_options)};
   auto rejected = node{runtime, std::move(rejected_options)};
   register_echo(server);
   const auto server_endpoint = listen(server, runtime);
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   occupying.peers().learn_endpoint(server.local_peer(), server_endpoint, pubsub_capabilities);
   rejected.peers().learn_endpoint(server.local_peer(), server_endpoint, pubsub_capabilities);

   const auto subject = pubsub::topic{.value = "forge.graft.capacity"};
   const auto warmup_subject = pubsub::topic{.value = "forge.graft.capacity.warmup"};
   const auto accept = [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
      co_return pubsub::validation_result::accept;
   };
   forge::asio::blocking::run(runtime, server.async_subscribe(subject, accept));

   auto occupying_stream = forge::asio::blocking::run(
       runtime, occupying.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   auto rejected_stream = forge::asio::blocking::run(
       runtime, rejected.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(runtime,
                              rejected_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                  .subscriptions = {pubsub::subscription{.subscribe = true, .subject = warmup_subject}},
                              })));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().peers != 1U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub PRUNE outbound warmup subscription");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);

   const auto warmup_messages_before = rejected.pubsub_snapshot().messages_received;
   static_cast<void>(
       forge::asio::blocking::run(runtime, server.async_publish(warmup_subject, std::vector<std::uint8_t>{0x37U})));
   for (auto poll = 0; poll < 200 && rejected.pubsub_snapshot().messages_received == warmup_messages_before; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub PRUNE outbound warmup delivery");
   }
   BOOST_REQUIRE(rejected.pubsub_snapshot().messages_received == warmup_messages_before + 1U);

   forge::asio::blocking::run(runtime,
                              occupying_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                  .subscriptions = {pubsub::subscription{.subscribe = true, .subject = subject}},
                              })));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().peers != 2U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub topic capacity fill");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 2U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 0U);

   const auto before_capacity_subscribe = server.metrics();
   const auto peers_before_capacity_subscribe = server.pubsub_snapshot().peers;
   forge::asio::blocking::run(runtime,
                              rejected_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                  .subscriptions = {pubsub::subscription{.subscribe = true, .subject = subject}},
                              })));
   for (auto poll = 0;
        poll < 200 && server.metrics().backpressure_rejections == before_capacity_subscribe.backpressure_rejections;
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub capacity SUBSCRIBE rejection");
   }
   BOOST_REQUIRE(server.metrics().backpressure_rejections == before_capacity_subscribe.backpressure_rejections + 1U);
   BOOST_TEST(server.metrics().protocol_rejections == before_capacity_subscribe.protocol_rejections + 1U);
   BOOST_TEST(server.metrics().pubsub_invalid_messages == before_capacity_subscribe.pubsub_invalid_messages);
   BOOST_TEST(server.metrics().active_sessions == before_capacity_subscribe.active_sessions);
   BOOST_TEST(server.pubsub_snapshot().peers == peers_before_capacity_subscribe);

   const auto server_before = server.pubsub_snapshot();
   const auto rejected_before = rejected.pubsub_snapshot();
   const auto server_metrics_before = server.metrics();
   const auto server_streams_before = server.metrics().protocol_streams_opened;
   BOOST_REQUIRE(rejected_before.mesh_edges == 0U);
   forge::asio::blocking::run(runtime, rejected_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                           .control_value =
                                               pubsub::control{
                                                   .grafts = {pubsub::control::graft{.subject = subject}},
                                               },
                                       })));
   for (auto poll = 0; poll < 200 && rejected.pubsub_snapshot().control_messages == rejected_before.control_messages;
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub capacity PRUNE delivery");
   }
   wait_on_runtime(runtime, std::chrono::milliseconds{25}, "GossipSub capacity PRUNE stability");

   const auto server_after = server.pubsub_snapshot();
   const auto rejected_after = rejected.pubsub_snapshot();
   BOOST_REQUIRE(rejected_after.control_messages == rejected_before.control_messages + 1U);
   BOOST_TEST(server_after.control_messages == server_before.control_messages + 1U);
   BOOST_TEST(server_after.peers == server_before.peers);
   BOOST_TEST(server_after.mesh_edges == 0U);
   BOOST_TEST(rejected_after.peers == rejected_before.peers);
   BOOST_TEST(rejected_after.mesh_edges == 0U);
   BOOST_TEST(server.metrics().protocol_streams_opened == server_streams_before);
   BOOST_TEST(server.metrics().pubsub_invalid_messages == server_metrics_before.pubsub_invalid_messages);
   BOOST_TEST(server.metrics().backpressure_rejections == server_metrics_before.backpressure_rejections + 1U);
   BOOST_TEST(server.metrics().active_sessions == server_metrics_before.active_sessions);

   forge::asio::blocking::run(runtime, rejected_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                           .control_value =
                                               pubsub::control{
                                                   .grafts = {pubsub::control::graft{.subject = subject}},
                                               },
                                       })));
   for (auto poll = 0; poll < 200 && rejected.pubsub_snapshot().control_messages == rejected_after.control_messages;
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub backoff violation PRUNE delivery");
   }
   wait_on_runtime(runtime, std::chrono::milliseconds{25}, "GossipSub backoff violation PRUNE stability");
   BOOST_REQUIRE(rejected.pubsub_snapshot().control_messages == rejected_after.control_messages + 1U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);
   BOOST_TEST(server.metrics().pubsub_invalid_messages == server_metrics_before.pubsub_invalid_messages + 1U);
   BOOST_TEST(server.metrics().active_sessions == server_metrics_before.active_sessions);

   auto echo = forge::asio::blocking::run(
       runtime, rejected.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                    node::open_options{.allow_relay = false}));
   const auto echo_payload = std::vector<std::uint8_t>{'l', 'i', 'v', 'e'};
   forge::asio::blocking::run(runtime, echo.async_write_frame(echo_payload));
   BOOST_TEST(forge::asio::blocking::run(runtime, echo.async_read_frame()) == echo_payload,
              boost::test_tools::per_element());
   forge::asio::blocking::run(runtime, echo.async_close());

   forge::asio::blocking::run(runtime,
                              occupying_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                  .subscriptions = {pubsub::subscription{.subscribe = false, .subject = subject}},
                              })));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().peers != 1U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub topic capacity release");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   wait_on_runtime(runtime, std::chrono::milliseconds{1'100}, "GossipSub local PRUNE backoff expiry");
   const auto control_after_violation = rejected.pubsub_snapshot().control_messages;
   forge::asio::blocking::run(runtime, rejected_stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                           .control_value =
                                               pubsub::control{
                                                   .grafts = {pubsub::control::graft{.subject = subject}},
                                               },
                                       })));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub post-backoff GRAFT admission");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_TEST(rejected.pubsub_snapshot().control_messages == control_after_violation);

   forge::asio::blocking::run(runtime, occupying_stream.async_close());
   forge::asio::blocking::run(runtime, rejected_stream.async_close());
   forge::asio::blocking::run(runtime, occupying.async_stop());
   forge::asio::blocking::run(runtime, rejected.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_received_prune_blocks_immediate_graft_until_backoff_expiry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for(make_test_certificate_identity("remote-prune-graft-server"));
   auto client_options = pubsub_options_for(make_test_certificate_identity("remote-prune-graft-client"));
   for (auto* options : {&server_options, &client_options}) {
      options->limits.pubsub.limits.prune_backoff = std::chrono::seconds{1};
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   }

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.graft.remote-prune"};
   const auto warmup_subject = pubsub::topic{.value = "forge.graft.remote-prune.warmup"};
   forge::asio::blocking::run(
       runtime, server.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   forge::asio::blocking::run(runtime,
                              stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                  .subscriptions = {pubsub::subscription{.subscribe = true, .subject = warmup_subject}},
                              })));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().peers != 1U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub remote PRUNE stream warmup");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   const auto client_messages_before = client.pubsub_snapshot().messages_received;
   static_cast<void>(
       forge::asio::blocking::run(runtime, server.async_publish(warmup_subject, std::vector<std::uint8_t>{0x42U})));
   for (auto poll = 0; poll < 200 && client.pubsub_snapshot().messages_received == client_messages_before; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub remote PRUNE reverse stream warmup");
   }
   BOOST_REQUIRE(client.pubsub_snapshot().messages_received == client_messages_before + 1U);

   const auto server_control_before_prune = server.pubsub_snapshot().control_messages;
   forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(pubsub::rpc{
                                           .control_value =
                                               pubsub::control{
                                                   .prunes = {pubsub::control::prune{
                                                       .subject = subject,
                                                       .backoff = std::chrono::seconds{1},
                                                   }},
                                               },
                                       })));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().control_messages == server_control_before_prune; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub remote PRUNE receipt");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().control_messages == server_control_before_prune + 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 0U);

   const auto server_before_graft = server.pubsub_snapshot();
   const auto client_before_graft = client.pubsub_snapshot();
   const auto metrics_before_graft = server.metrics();
   const auto streams_before_graft = server.metrics().protocol_streams_opened;
   const auto graft = pubsub::codec::encode(pubsub::rpc{
       .control_value = pubsub::control{.grafts = {pubsub::control::graft{.subject = subject}}},
   });
   forge::asio::blocking::run(runtime, stream.async_write(graft));
   for (auto poll = 0; poll < 200 && client.pubsub_snapshot().control_messages == client_before_graft.control_messages;
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub remote-backoff PRUNE delivery");
   }

   const auto server_after_rejection = server.pubsub_snapshot();
   const auto client_after_rejection = client.pubsub_snapshot();
   BOOST_REQUIRE(client_after_rejection.control_messages == client_before_graft.control_messages + 1U);
   BOOST_TEST(server_after_rejection.control_messages == server_before_graft.control_messages + 1U);
   BOOST_TEST(server_after_rejection.peers == server_before_graft.peers);
   BOOST_TEST(server_after_rejection.mesh_edges == 0U);
   BOOST_TEST(client_after_rejection.peers == client_before_graft.peers);
   BOOST_TEST(client_after_rejection.mesh_edges == client_before_graft.mesh_edges);
   BOOST_TEST(server.metrics().pubsub_invalid_messages == metrics_before_graft.pubsub_invalid_messages + 1U);
   BOOST_TEST(server.metrics().protocol_rejections == metrics_before_graft.protocol_rejections + 1U);
   BOOST_TEST(server.metrics().protocol_streams_opened == streams_before_graft);

   wait_on_runtime(runtime, std::chrono::milliseconds{1'100}, "GossipSub unified PRUNE backoff expiry");
   const auto server_control_after_expiry = server.pubsub_snapshot().control_messages;
   const auto client_control_after_expiry = client.pubsub_snapshot().control_messages;
   forge::asio::blocking::run(runtime, stream.async_write(graft));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub post-remote-backoff GRAFT admission");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_TEST(server.pubsub_snapshot().control_messages == server_control_after_expiry + 1U);
   BOOST_TEST(client.pubsub_snapshot().control_messages == client_control_after_expiry);
   BOOST_TEST(server.metrics().protocol_streams_opened == streams_before_graft);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_unsubscribe_sends_leave_and_enforces_backoff_until_expiry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for(make_test_certificate_identity("unsubscribe-leave-server"));
   auto client_options = pubsub_options_for(make_test_certificate_identity("unsubscribe-leave-client"));
   for (auto* options : {&server_options, &client_options}) {
      options->limits.pubsub.limits.unsubscribe_backoff = std::chrono::seconds{1};
      options->limits.pubsub.limits.prune_backoff = std::chrono::seconds{1};
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::seconds{60};
   }

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   const auto subject = pubsub::topic{.value = "forge.unsubscribe.leave"};
   const auto accept = [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
      co_return pubsub::validation_result::accept;
   };
   forge::asio::blocking::run(runtime, server.async_subscribe(subject, accept));
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto subscribe_and_graft = pubsub::codec::encode(pubsub::rpc{
       .subscriptions = {pubsub::subscription{.subscribe = true, .subject = subject}},
       .control_value = pubsub::control{.grafts = {pubsub::control::graft{.subject = subject}}},
   });
   forge::asio::blocking::run(runtime, stream.async_write(subscribe_and_graft));
   for (auto poll = 0;
        poll < 200 && (server.pubsub_snapshot().peers != 1U || server.pubsub_snapshot().mesh_edges != 1U); ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub unsubscribe mesh setup");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);

   const auto client_messages_before = client.pubsub_snapshot().messages_received;
   static_cast<void>(
       forge::asio::blocking::run(runtime, server.async_publish(subject, std::vector<std::uint8_t>{0x4cU})));
   for (auto poll = 0; poll < 200 && (client.pubsub_snapshot().messages_received == client_messages_before ||
                                      client.pubsub_snapshot().peers != 1U);
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub unsubscribe reverse stream warmup");
   }
   BOOST_REQUIRE(client.pubsub_snapshot().messages_received == client_messages_before + 1U);
   BOOST_REQUIRE(client.pubsub_snapshot().peers == 1U);

   const auto client_before_leave = client.pubsub_snapshot();
   const auto server_streams_before_leave = server.metrics().protocol_streams_opened;
   const auto client_streams_before_leave = client.metrics().protocol_streams_opened;
   forge::asio::blocking::run(runtime, server.async_unsubscribe(subject));
   for (auto poll = 0;
        poll < 200 && (client.pubsub_snapshot().peers != 0U ||
                       client.pubsub_snapshot().control_messages == client_before_leave.control_messages);
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub unsubscribe LEAVE delivery");
   }
   const auto client_after_leave = client.pubsub_snapshot();
   BOOST_REQUIRE(client_after_leave.peers == 0U);
   BOOST_REQUIRE(client_after_leave.control_messages == client_before_leave.control_messages + 1U);
   BOOST_TEST(client_after_leave.mesh_edges == 0U);
   BOOST_TEST(server.pubsub_snapshot().topics == 0U);
   BOOST_TEST(server.pubsub_snapshot().peers == 1U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);
   BOOST_TEST(server.metrics().protocol_streams_opened == server_streams_before_leave);
   BOOST_TEST(client.metrics().protocol_streams_opened == client_streams_before_leave);

   forge::asio::blocking::run(runtime, server.async_subscribe(subject, accept));
   for (auto poll = 0; poll < 200 && client.pubsub_snapshot().peers != 1U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub post-unsubscribe resubscribe delivery");
   }
   BOOST_REQUIRE(client.pubsub_snapshot().peers == 1U);
   const auto server_before_reentry = server.pubsub_snapshot();
   const auto client_before_reentry = client.pubsub_snapshot();
   const auto server_metrics_before_reentry = server.metrics();
   const auto graft = pubsub::codec::encode(pubsub::rpc{
       .control_value = pubsub::control{.grafts = {pubsub::control::graft{.subject = subject}}},
   });
   forge::asio::blocking::run(runtime, stream.async_write(graft));
   for (auto poll = 0;
        poll < 200 && client.pubsub_snapshot().control_messages == client_before_reentry.control_messages; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub unsubscribe-backoff PRUNE delivery");
   }
   BOOST_REQUIRE(client.pubsub_snapshot().control_messages == client_before_reentry.control_messages + 1U);
   BOOST_TEST(server.pubsub_snapshot().control_messages == server_before_reentry.control_messages + 1U);
   BOOST_TEST(server.pubsub_snapshot().peers == 1U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);
   BOOST_TEST(server.metrics().pubsub_invalid_messages == server_metrics_before_reentry.pubsub_invalid_messages + 1U);
   BOOST_TEST(server.metrics().protocol_streams_opened == server_streams_before_leave);
   BOOST_TEST(client.metrics().protocol_streams_opened == client_streams_before_leave);

   wait_on_runtime(runtime, std::chrono::milliseconds{1'100}, "GossipSub unsubscribe backoff expiry");
   const auto server_control_after_expiry = server.pubsub_snapshot().control_messages;
   const auto client_control_after_expiry = client.pubsub_snapshot().control_messages;
   forge::asio::blocking::run(runtime, stream.async_write(graft));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub post-unsubscribe-backoff GRAFT admission");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_TEST(server.pubsub_snapshot().control_messages == server_control_after_expiry + 1U);
   BOOST_TEST(client.pubsub_snapshot().control_messages == client_control_after_expiry);
   BOOST_TEST(server.metrics().protocol_streams_opened == server_streams_before_leave);
   BOOST_TEST(client.metrics().protocol_streams_opened == client_streams_before_leave);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_heartbeat_obeys_sent_and_received_prune_backoff_until_expiry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   auto server_options = pubsub_options_for(make_test_certificate_identity("heartbeat-backoff-server"));
   server_options.limits.pubsub.limits.mesh_n = 1;
   server_options.limits.pubsub.limits.mesh_n_low = 1;
   server_options.limits.pubsub.limits.mesh_n_high = 1;
   server_options.limits.pubsub.limits.prune_backoff = std::chrono::seconds{1};
   server_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{2'000};
   server_options.limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{400};
   auto first_options = pubsub_options_for(make_test_certificate_identity("heartbeat-backoff-first"));
   auto second_options = pubsub_options_for(make_test_certificate_identity("heartbeat-backoff-second"));
   for (auto* options : {&first_options, &second_options}) {
      options->limits.pubsub.limits.prune_backoff = std::chrono::seconds{1};
      options->limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{2'050};
      options->limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{50};
   }

   auto server = node{runtime, std::move(server_options)};
   auto first = node{runtime, std::move(first_options)};
   auto second = node{runtime, std::move(second_options)};
   const auto server_endpoint = listen(server, runtime);
   (void)listen(first, runtime);
   (void)listen(second, runtime);
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   first.peers().learn_endpoint(server.local_peer(), server_endpoint, pubsub_capabilities);
   second.peers().learn_endpoint(server.local_peer(), server_endpoint, pubsub_capabilities);

   const auto subject = pubsub::topic{.value = "forge.graft.heartbeat-backoff"};
   const auto accept = [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
      co_return pubsub::validation_result::accept;
   };
   forge::asio::blocking::run(runtime, server.async_subscribe(subject, accept));
   forge::asio::blocking::run(runtime, first.async_subscribe(subject, accept));
   forge::asio::blocking::run(runtime, second.async_subscribe(subject, accept));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().peers != 2U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub heartbeat backoff subscriptions");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 2U);

   auto first_stream = forge::asio::blocking::run(
       runtime, first.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   auto second_stream = forge::asio::blocking::run(
       runtime, second.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto graft = pubsub::codec::encode(pubsub::rpc{
       .control_value = pubsub::control{.grafts = {pubsub::control::graft{.subject = subject}}},
   });
   forge::asio::blocking::run(runtime, first_stream.async_write(graft));
   forge::asio::blocking::run(runtime, second_stream.async_write(graft));
   for (auto poll = 0; poll < 200 && server.pubsub_snapshot().mesh_edges != 2U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub heartbeat oversubscription setup");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 2U);

   const auto first_is_pruned = first.local_peer() > second.local_peer();
   auto* pruned = first_is_pruned ? &first : &second;
   auto* retained = first_is_pruned ? &second : &first;
   auto* pruned_stream = first_is_pruned ? &first_stream : &second_stream;
   auto* retained_stream = first_is_pruned ? &second_stream : &first_stream;
   const auto pruned_control_before = pruned->pubsub_snapshot().control_messages;
   for (auto poll = 0; poll < 600 && pruned->pubsub_snapshot().control_messages == pruned_control_before; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub heartbeat PRUNE delivery");
   }
   BOOST_REQUIRE(pruned->pubsub_snapshot().control_messages == pruned_control_before + 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_REQUIRE(pruned->pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(pruned->pubsub_snapshot().mesh_edges == 0U);

   const auto sessions_before_cleanup = server.metrics().active_sessions;
   BOOST_REQUIRE(sessions_before_cleanup >= 2U);
   forge::asio::blocking::run(runtime, retained_stream->async_close());
   forge::asio::blocking::run(runtime, retained->async_stop());
   for (auto poll = 0;
        poll < 400 && (server.pubsub_snapshot().peers != 1U || server.pubsub_snapshot().mesh_edges != 0U ||
                       server.metrics().active_sessions >= sessions_before_cleanup);
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub retained peer cleanup");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 0U);
   BOOST_REQUIRE(server.metrics().active_sessions + 1U == sessions_before_cleanup);

   const auto server_control_during_backoff = server.pubsub_snapshot().control_messages;
   const auto pruned_control_during_backoff = pruned->pubsub_snapshot().control_messages;
   const auto invalid_before_backoff = server.pubsub_snapshot().invalid_messages;

   wait_on_runtime(runtime, std::chrono::milliseconds{300}, "GossipSub remote PRUNE backoff heartbeat ticks");
   BOOST_TEST(server.pubsub_snapshot().control_messages == server_control_during_backoff);
   BOOST_TEST(pruned->pubsub_snapshot().control_messages == pruned_control_during_backoff);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);
   BOOST_TEST(pruned->pubsub_snapshot().mesh_edges == 0U);
   BOOST_TEST(server.pubsub_snapshot().invalid_messages == invalid_before_backoff);

   for (auto poll = 0; poll < 400 && server.pubsub_snapshot().control_messages == server_control_during_backoff;
        ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub remote PRUNE backoff expiry");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().control_messages == server_control_during_backoff + 1U);
   BOOST_REQUIRE(pruned->pubsub_snapshot().control_messages == pruned_control_during_backoff);
   BOOST_TEST(server.pubsub_snapshot().peers == 1U);
   BOOST_TEST(pruned->pubsub_snapshot().peers == 1U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_TEST(pruned->pubsub_snapshot().mesh_edges == 1U);
   BOOST_TEST(server.pubsub_snapshot().invalid_messages == invalid_before_backoff);

   forge::asio::blocking::run(runtime, pruned_stream->async_close());
   forge::asio::blocking::run(runtime, pruned->async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_heartbeat_does_not_graft_unsubscribed_sessions) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
   server_options.limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
   auto client_options = pubsub_options_for();
   client_options.explicit_peer_id = peer(220);

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto subject = pubsub::topic{.value = "forge.subscription.absent"};
   forge::asio::blocking::run(
       runtime, server.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   wait_on_runtime(runtime, std::chrono::milliseconds{150}, "GossipSub unsubscribed heartbeat stability");
   BOOST_TEST(server.pubsub_snapshot().peers == 0U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_graft_records_subscription_through_heartbeat) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
   server_options.limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
   auto client_options = pubsub_options_for();
   client_options.explicit_peer_id = peer(225);

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto subject = pubsub::topic{.value = "forge.graft.subscription"};
   forge::asio::blocking::run(
       runtime, server.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   const auto graft = pubsub::codec::encode(pubsub::rpc{
       .control_value =
           pubsub::control{.grafts = std::vector<pubsub::control::graft>{pubsub::control::graft{.subject = subject}}},
   });
   forge::asio::blocking::run(runtime, stream.async_write(graft));
   for (auto poll = 0; poll < 100 && server.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub GRAFT-only admission");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);

   forge::asio::blocking::run(runtime, stream.async_write(graft));
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "GossipSub GRAFT-only heartbeat stability");
   BOOST_TEST(server.pubsub_snapshot().peers == 1U);
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 1U);

   const auto unsubscribe = pubsub::codec::encode(pubsub::rpc{
       .subscriptions =
           std::vector<pubsub::subscription>{
               pubsub::subscription{.subscribe = false, .subject = subject},
           },
   });
   forge::asio::blocking::run(runtime, stream.async_write(unsubscribe));
   for (auto poll = 0; poll < 100 && server.pubsub_snapshot().mesh_edges != 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub GRAFT-only unsubscribe");
   }
   BOOST_TEST(server.pubsub_snapshot().mesh_edges == 0U);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_protocol_stream_close_preserves_peer_state_until_session_disconnect) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.pubsub.limits.heartbeat_initial_delay = std::chrono::milliseconds{10};
   server_options.limits.pubsub.limits.heartbeat_interval = std::chrono::milliseconds{20};
   auto client_options = pubsub_options_for();
   client_options.explicit_peer_id = peer(221);

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto first_stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto subject = pubsub::topic{.value = "forge.subscription.disconnect"};
   forge::asio::blocking::run(
       runtime, server.async_subscribe(subject, [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
          co_return pubsub::validation_result::accept;
       }));

   const auto subscribe_and_graft = pubsub::codec::encode(pubsub::rpc{
       .subscriptions =
           std::vector<pubsub::subscription>{
               pubsub::subscription{.subscribe = true, .subject = subject},
           },
       .control_value =
           pubsub::control{
               .grafts =
                   std::vector<pubsub::control::graft>{
                       pubsub::control::graft{.subject = subject},
                   },
           },
   });
   forge::asio::blocking::run(runtime, first_stream.async_write(subscribe_and_graft));
   for (auto poll = 0; poll < 100 && server.pubsub_snapshot().mesh_edges == 0U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub initial subscription and GRAFT");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);

   forge::asio::blocking::run(runtime, first_stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "GossipSub transient protocol stream close");
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_REQUIRE(server.metrics().active_sessions > 0U);

   auto second_stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto graft = pubsub::codec::encode(pubsub::rpc{
       .control_value =
           pubsub::control{
               .grafts =
                   std::vector<pubsub::control::graft>{
                       pubsub::control::graft{.subject = subject},
                   },
           },
   });
   forge::asio::blocking::run(runtime, second_stream.async_write(graft));
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "GossipSub reopened stream heartbeat stability");
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 1U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 1U);
   BOOST_REQUIRE(server.metrics().active_sessions > 0U);

   forge::asio::blocking::run(runtime, client.async_stop());
   for (auto poll = 0;
        poll < 200 && (server.pubsub_snapshot().peers != 0U || server.pubsub_snapshot().mesh_edges != 0U); ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "GossipSub final session disconnect cleanup");
   }
   BOOST_REQUIRE(server.pubsub_snapshot().peers == 0U);
   BOOST_REQUIRE(server.pubsub_snapshot().mesh_edges == 0U);
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_control_spam_is_penalized_without_stopping_node) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.pubsub.limits.max_ihave_per_peer = 1;
   server_options.limits.pubsub.limits.max_iwant_per_peer = 1;
   server_options.limits.pubsub.limits.max_graft_per_peer = 1;
   auto client_options = pubsub_options_for();
   client_options.explicit_peer_id = peer(160);

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   forge::asio::blocking::run(
       runtime, server.async_subscribe(pubsub::topic{.value = "forge.spam"},
                                       [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
                                          co_return pubsub::validation_result::accept;
                                       }));

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto id = std::vector<std::uint8_t>{'i', 'd'};
   auto spam =
       pubsub::rpc{
           .control_value =
               pubsub::control{
                   .have =
                       std::vector<pubsub::control::ihave>{
                           pubsub::control::ihave{.subject = pubsub::topic{.value = "forge.spam"},
                                                  .message_ids = std::vector<std::vector<std::uint8_t>>{id}},
                           pubsub::control::ihave{.subject = pubsub::topic{.value = "forge.spam"},
                                                  .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                   .want =
                       std::vector<pubsub::control::iwant>{
                           pubsub::control::iwant{.message_ids = std::vector<std::vector<std::uint8_t>>{id}},
                           pubsub::control::iwant{.message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                   .grafts =
                       std::vector<pubsub::control::graft>{
                           pubsub::control::graft{.subject = pubsub::topic{.value = "forge.spam"}},
                           pubsub::control::graft{.subject = pubsub::topic{.value = "forge.spam"}}},
               },
       };
   forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(spam)));
   wait_on_runtime(runtime, std::chrono::milliseconds{250}, "gossipsub spam accounting");

   BOOST_TEST(server.metrics().pubsub_invalid_messages >= 1U);
   BOOST_TEST(server.metrics().protocol_rejections >= 1U);
   BOOST_TEST(!server.metrics().stopped);

   forge::asio::blocking::run(runtime, stream.async_close());
   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_abusive_peer_crossing_malformed_threshold_closes_only_offender_session) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.resources.max_malformed_messages_per_peer = 1;
   server_options.limits.max_sessions = 2;
   server_options.limits.max_inbound_sessions = 2;
   server_options.limits.session_low_watermark = 2;
   server_options.limits.pubsub.limits.max_ihave_per_peer = 1;
   auto bad_options = pubsub_options_for();
   bad_options.explicit_peer_id = peer(248);
   auto good_options = pubsub_options_for();
   good_options.explicit_peer_id = peer(249);

   auto server = node{runtime, std::move(server_options)};
   auto bad = node{runtime, std::move(bad_options)};
   auto good = node{runtime, std::move(good_options)};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   bad.peers().learn_endpoint(server.local_peer(), server_endpoint,
                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   good.peers().learn_endpoint(server.local_peer(), server_endpoint,
                               capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   (void)forge::asio::blocking::run(
       runtime, bad.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   (void)forge::asio::blocking::run(
       runtime, good.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto id = std::vector<std::uint8_t>{'i', 'd'};
   auto spam = pubsub::rpc{
       .control_value =
           pubsub::control{
               .have =
                   std::vector<pubsub::control::ihave>{
                       pubsub::control::ihave{.subject = pubsub::topic{.value = "forge.abuse"},
                                              .message_ids = std::vector<std::vector<std::uint8_t>>{id}},
                       pubsub::control::ihave{.subject = pubsub::topic{.value = "forge.abuse"},
                                              .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
           },
   };
   for (auto index = 0; index != 2; ++index) {
      auto stream = forge::asio::blocking::run(
          runtime, bad.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
      forge::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(spam)));
      wait_on_runtime(runtime, std::chrono::milliseconds{150}, "gossipsub abuse accounting");
   }

   BOOST_TEST(server.metrics().pubsub_invalid_messages >= 2U);
   BOOST_TEST(server.metrics().sessions_closed >= 1U);
   BOOST_TEST(server.metrics().connection_rejections >= 1U);
   const auto after_eviction = server.diagnostics();
   BOOST_TEST(after_eviction.connections.retained_identify_attempts <= after_eviction.connections.active_sessions);

   auto stream =
       forge::asio::blocking::run(runtime, good.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                                           node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'g', 'o', 'o', 'd'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, good.async_stop());
   forge::asio::blocking::run(runtime, bad.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_outbound_byte_limit_rejects_publish_without_stopping_node) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   publisher_options.limits.pubsub.limits.max_outbound_queue_bytes = 8;
   auto subscriber_options = pubsub_options_for();
   subscriber_options.explicit_peer_id = peer(161);

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(pubsub::topic{.value = "forge.limit"},
                                           [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
                                              co_return pubsub::validation_result::accept;
                                           }));

   BOOST_CHECK_THROW((void)forge::asio::blocking::run(
                         runtime, publisher.async_publish(pubsub::topic{.value = "forge.limit"},
                                                          std::vector<std::uint8_t>{'o', 'v', 'e', 'r'})),
                     forge::exceptions::base);
   BOOST_TEST(publisher.metrics().backpressure_rejections >= 1U);
   BOOST_TEST(!publisher.metrics().stopped);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   forge::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_outbound_byte_limit_counts_blocked_active_publication_per_peer) {
   constexpr auto queue_limit = std::size_t{128 * 1024};
   constexpr auto payload_size = std::size_t{96 * 1024};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_identity();
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   auto publisher_options = options_for(publisher_identity, pubsub_capabilities);
   publisher_options.limits.pubsub.limits.max_outbound_queue_bytes = queue_limit;
   auto publisher = node{runtime, std::move(publisher_options)};

   auto accepted = std::make_shared<std::promise<void>>();
   auto accepted_future = accepted->get_future();
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime, std::chrono::seconds{5}, accepted);
   const auto stalled_peer = peer(162);
   publisher.peers().learn_endpoint(stalled_peer, stalled_endpoint, pubsub_capabilities);
   const auto failures_before = publisher.peers().find(stalled_peer)->failures;
   const auto subject = pubsub::topic{.value = "forge.limit.aggregate"};
   auto first = boost::asio::co_spawn(runtime.context(),
                                      publisher.async_publish(subject, std::vector<std::uint8_t>(payload_size, 0x31U)),
                                      boost::asio::use_future);
   BOOST_REQUIRE(accepted_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);

   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, publisher.async_publish(subject, std::vector<std::uint8_t>(payload_size, 0x32U))));
      BOOST_FAIL("aggregate GossipSub outbound byte limit should reject the queued publication");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(forge::net::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*forge::net::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   BOOST_TEST(publisher.metrics().backpressure_rejections >= 1U);
   BOOST_REQUIRE(publisher.peers().find(stalled_peer).has_value());
   BOOST_TEST(publisher.peers().find(stalled_peer)->failures == failures_before);
   const auto metrics_before_stop = publisher.metrics();
   const auto peer_before_stop = publisher.peers().find(stalled_peer);
   BOOST_REQUIRE(peer_before_stop.has_value());
   BOOST_REQUIRE(!peer_before_stop->endpoints.empty());
   const auto endpoint_failures_before_stop = peer_before_stop->endpoints.front().failures;
   const auto endpoint_backoff_before_stop = peer_before_stop->endpoints.front().backoff_until;

   forge::asio::blocking::run(runtime, publisher.async_stop());
   BOOST_REQUIRE(first.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_THROW(static_cast<void>(first.get()), forge::exceptions::base);
   const auto metrics_after_stop = publisher.metrics();
   const auto peer_after_stop = publisher.peers().find(stalled_peer);
   BOOST_REQUIRE(peer_after_stop.has_value());
   BOOST_REQUIRE(!peer_after_stop->endpoints.empty());
   BOOST_TEST(peer_after_stop->failures == failures_before);
   BOOST_TEST(peer_after_stop->endpoints.front().failures == endpoint_failures_before_stop);
   const auto backoff_unchanged_during_stop =
       peer_after_stop->endpoints.front().backoff_until == endpoint_backoff_before_stop;
   BOOST_TEST(backoff_unchanged_during_stop);
   BOOST_TEST(metrics_after_stop.direct_failures == metrics_before_stop.direct_failures);
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "post-stop GossipSub dial drain");
   const auto peer_after_drain = publisher.peers().find(stalled_peer);
   BOOST_REQUIRE(peer_after_drain.has_value());
   BOOST_REQUIRE(!peer_after_drain->endpoints.empty());
   BOOST_TEST(peer_after_drain->failures == failures_before);
   BOOST_TEST(peer_after_drain->endpoints.front().failures == endpoint_failures_before_stop);
   const auto backoff_unchanged_after_drain =
       peer_after_drain->endpoints.front().backoff_until == endpoint_backoff_before_stop;
   BOOST_TEST(backoff_unchanged_after_drain);
   BOOST_TEST(publisher.metrics().direct_failures == metrics_after_stop.direct_failures);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_outbound_byte_limit_is_global_across_peers) {
   constexpr auto queue_limit = std::size_t{128 * 1024};
   constexpr auto payload_size = std::size_t{96 * 1024};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto publisher_identity = make_test_identity();
   const auto pubsub_capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::pubsub};
   auto publisher_options = options_for(publisher_identity, pubsub_capabilities);
   publisher_options.limits.pubsub.limits.max_outbound_queue_bytes = queue_limit;
   auto publisher = node{runtime, std::move(publisher_options)};

   auto accepted_a = std::make_shared<std::promise<void>>();
   auto accepted_b = std::make_shared<std::promise<void>>();
   auto accepted_a_future = accepted_a->get_future();
   auto accepted_b_future = accepted_b->get_future();
   const auto endpoint_a = start_stalling_tcp_peer(runtime, std::chrono::seconds{5}, accepted_a);
   const auto endpoint_b = start_stalling_tcp_peer(runtime, std::chrono::seconds{5}, accepted_b);
   const auto peer_a = peer(163);
   const auto peer_b = peer(164);
   publisher.peers().learn_endpoint(peer_a, endpoint_a, pubsub_capabilities);

   const auto subject = pubsub::topic{.value = "forge.limit.global"};
   auto first = boost::asio::co_spawn(runtime.context(),
                                      publisher.async_publish(subject, std::vector<std::uint8_t>(payload_size, 0x41U)),
                                      boost::asio::use_future);
   BOOST_REQUIRE(accepted_a_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);

   publisher.peers().upsert(peer_store::record{.peer = peer_a});
   publisher.peers().learn_endpoint(peer_b, endpoint_b, pubsub_capabilities);
   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, publisher.async_publish(subject, std::vector<std::uint8_t>(payload_size, 0x42U))));
      BOOST_FAIL("global GossipSub outbound byte limit should reject a different peer");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_CHECK(*exceptions::code_of(error) == exceptions::code::backpressure_rejected);
   }
   const auto second_accept_is_pending =
       accepted_b_future.wait_for(std::chrono::milliseconds{50}) != std::future_status::ready;
   BOOST_TEST(second_accept_is_pending);

   forge::asio::blocking::run(runtime, publisher.async_stop());
   BOOST_REQUIRE(first.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_THROW(static_cast<void>(first.get()), forge::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_rejected_outbound_reservations_do_not_retain_peer_entries) {
   auto budget = detail::pubsub_outbound_budget{};
   constexpr auto limit = std::size_t{128};
   constexpr auto reservation = std::size_t{96};
   const auto active = peer(250);
   BOOST_REQUIRE(budget.reserve(active, reservation, limit));

   for (auto seed = std::uint16_t{1}; seed <= 200; ++seed) {
      BOOST_TEST(!budget.reserve(peer(static_cast<std::uint8_t>(seed)), reservation, limit));
   }
   BOOST_TEST(budget.peers() == 1U);
   BOOST_TEST(budget.total() == reservation);

   budget.release(active, reservation);
   BOOST_TEST(budget.peers() == 0U);
   BOOST_TEST(budget.total() == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_remote_peer_failure_attribution_excludes_local_runtime_failures) {
   BOOST_TEST(!detail::remote_peer_attributable_failure(exceptions::code::backpressure_rejected, false));
   BOOST_TEST(!detail::remote_peer_attributable_failure(exceptions::code::canceled, false));
   BOOST_TEST(!detail::remote_peer_attributable_failure(exceptions::code::internal, false));
   BOOST_TEST(!detail::remote_peer_attributable_failure(exceptions::code::sequence_exhausted, false));
   BOOST_TEST(!detail::remote_peer_attributable_failure(exceptions::code::durability_uncertain, false));
   BOOST_TEST(!detail::remote_peer_attributable_failure(exceptions::code::closed, true));
   BOOST_TEST(detail::remote_peer_attributable_failure(exceptions::code::timeout, true));
   BOOST_TEST(detail::remote_peer_attributable_failure(exceptions::code::closed, false));
   BOOST_TEST(detail::remote_peer_attributable_failure(exceptions::code::protocol_error, false));
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_validation_queue_limit_retries_excess_without_penalizing_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   auto subscriber_options = pubsub_options_for(make_test_certificate_identity("pubsub-validation-subscriber"));
   subscriber_options.limits.pubsub.limits.max_validation_queue = 1;
   subscriber_options.limits.pubsub.limits.max_validation_attempts = 1;
   subscriber_options.limits.pubsub.limits.validation_retry_initial_delay = std::chrono::milliseconds{600};
   subscriber_options.limits.pubsub.limits.validation_retry_max_delay = std::chrono::milliseconds{600};
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   auto publisher_a_options = pubsub_options_for(make_test_certificate_identity("pubsub-validation-a"));
   publisher_a_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   auto publisher_b_options = pubsub_options_for(make_test_certificate_identity("pubsub-validation-b"));
   publisher_b_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;

   auto subscriber = node{runtime, std::move(subscriber_options)};
   auto publisher_a = node{runtime, std::move(publisher_a_options)};
   auto publisher_b = node{runtime, std::move(publisher_b_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher_a.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                      capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   publisher_b.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                      capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto entered = std::make_shared<std::atomic_uint64_t>(0);
   forge::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    pubsub::topic{.value = "forge.validation"},
                    [entered](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       entered->fetch_add(1, std::memory_order_relaxed);
                       auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
                       timer.expires_after(std::chrono::milliseconds{500});
                       boost::system::error_code ec;
                       co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
                       co_return pubsub::validation_result::accept;
                    }));

   auto publish_a = boost::asio::co_spawn(
       runtime.context(),
       [&publisher_a]() -> boost::asio::awaitable<void> {
          (void)co_await publisher_a.async_publish(pubsub::topic{.value = "forge.validation"},
                                                   std::vector<std::uint8_t>{'a'},
                                                   pubsub::publish_options{.sign = false});
       },
       boost::asio::use_future);
   auto publish_b = boost::asio::co_spawn(
       runtime.context(),
       [&publisher_b]() -> boost::asio::awaitable<void> {
          (void)co_await publisher_b.async_publish(pubsub::topic{.value = "forge.validation"},
                                                   std::vector<std::uint8_t>{'b'},
                                                   pubsub::publish_options{.sign = false});
       },
       boost::asio::use_future);
   wait_for_server(publish_a, std::chrono::seconds{5}, "first validation publish");
   wait_for_server(publish_b, std::chrono::seconds{5}, "second validation publish");
   for (auto poll = 0; poll < 100 && entered->load(std::memory_order_relaxed) < 2U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{20}, "validation queue retry");
   }
   for (auto poll = 0; poll < 100 && subscriber.pubsub_snapshot().messages_delivered < 2U; ++poll) {
      wait_on_runtime(runtime, std::chrono::milliseconds{20}, "validation completion");
   }

   BOOST_TEST(entered->load(std::memory_order_relaxed) == 2U);
   BOOST_TEST(subscriber.metrics().backpressure_rejections >= 1U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages == 0U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 2U);

   forge::asio::blocking::run(runtime, subscriber.async_stop());
   BOOST_TEST(subscriber.metrics().stopped);
   forge::asio::blocking::run(runtime, publisher_a.async_stop());
   forge::asio::blocking::run(runtime, publisher_b.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_ten_node_mesh_delivers_multiple_publishes_once) {
   constexpr auto node_count = std::size_t{10};
   constexpr auto publish_count = std::size_t{3};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 12}};
   auto identities = std::vector<test_certificate_identity>{};
   auto nodes = std::vector<std::unique_ptr<node>>{};
   auto endpoints = std::vector<endpoint>{};
   identities.reserve(node_count);
   nodes.reserve(node_count);
   endpoints.reserve(node_count);
   for (auto index = std::size_t{}; index < node_count; ++index) {
      identities.push_back(make_test_certificate_identity("pubsub-mesh-" + std::to_string(index)));
      auto options = pubsub_options_for(identities.back());
      options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
      nodes.push_back(std::make_unique<node>(runtime, std::move(options)));
      endpoints.push_back(listen(*nodes.back(), runtime));
   }
   for (auto index = std::size_t{}; index < node_count; ++index) {
      for (auto peer_index = std::size_t{}; peer_index < node_count; ++peer_index) {
         if (index == peer_index) {
            continue;
         }
         nodes[index]->peers().learn_endpoint(nodes[peer_index]->local_peer(), endpoints[peer_index],
                                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
      }
   }

   struct delivery_state {
      std::mutex mutex;
      std::condition_variable cv;
      std::map<std::string, std::set<std::size_t>> delivered;
      std::uint64_t duplicates = 0;
   };
   auto state = std::make_shared<delivery_state>();
   for (auto index = std::size_t{}; index < node_count; ++index) {
      forge::asio::blocking::run(
          runtime,
          nodes[index]->async_subscribe(
              pubsub::topic{.value = "forge.mesh.stress"},
              [state, index](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                 const auto payload = std::string{event.value.data.begin(), event.value.data.end()};
                 {
                    auto lock = std::unique_lock{state->mutex};
                    if (!state->delivered[payload].insert(index).second) {
                       ++state->duplicates;
                    }
                 }
                 state->cv.notify_all();
                 co_return pubsub::validation_result::accept;
              }));
   }

   for (auto index = std::size_t{}; index < publish_count; ++index) {
      const auto payload = std::string{"stress-" + std::to_string(index)};
      forge::asio::blocking::run(runtime,
                                 nodes[index]->async_publish(pubsub::topic{.value = "forge.mesh.stress"},
                                                             std::vector<std::uint8_t>{payload.begin(), payload.end()},
                                                             pubsub::publish_options{.sign = false}));
   }

   {
      auto lock = std::unique_lock{state->mutex};
      const auto completed = state->cv.wait_for(lock, std::chrono::seconds{15}, [&] {
         for (auto index = std::size_t{}; index < publish_count; ++index) {
            const auto payload = std::string{"stress-" + std::to_string(index)};
            if (state->delivered[payload].size() < node_count - 1) {
               return false;
            }
         }
         return true;
      });
      BOOST_REQUIRE(completed);
      BOOST_TEST(state->duplicates == 0U);
   }

   for (auto index = std::size_t{}; index < publish_count; ++index) {
      const auto payload = std::string{"stress-" + std::to_string(index)};
      BOOST_TEST(!state->delivered[payload].contains(index));
   }
   for (auto& value : nodes) {
      forge::asio::blocking::run(runtime, value->async_stop());
   }
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_updates_peer_store) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-push-update-server");
   const auto client_identity = make_test_certificate_identity("identify-push-update-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   const auto learned_endpoint =
       parse_endpoint("/ip4/127.0.0.1/udp/4099/quic-v1/p2p/" + client.local_peer().to_string());
   server.peers().learn_endpoint(client.local_peer(), learned_endpoint);

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto pushed = identify::document{
       .protocol_version = "/forge/push-test/1",
       .agent_version = "forge-push-test/1",
       .listen_endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4101/quic-v1/p2p/" +
                                                                client.local_peer().to_string())},
       .protocols = std::vector<protocol_id>{builtins::ping},
   };
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push propagation");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/push-test/1");
   BOOST_TEST(
       std::ranges::any_of(found->protocols, [](const protocol_id& protocol) { return protocol == builtins::ping; }));
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 2U);
   const auto identified =
       std::ranges::find_if(found->endpoints, [](const auto& value) { return value.endpoint.transport.port == 4101U; });
   BOOST_REQUIRE(identified != found->endpoints.end());
   BOOST_TEST(identified->endpoint.peer->to_bytes() == client.local_peer().to_bytes(),
              boost::test_tools::per_element());
   BOOST_TEST(!identified->sources.learned);
   BOOST_TEST(identified->sources.identify_unsigned);
   BOOST_TEST(!identified->sources.identify_signed);

   const auto replacement = parse_endpoint("/ip4/127.0.0.1/udp/4102/quic-v1/p2p/" + client.local_peer().to_string());
   stream = forge::asio::blocking::run(runtime,
                                       client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   pushed.listen_endpoints = {replacement};
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "Identify unsigned snapshot replacement");

   const auto replaced = server.peers().find(client.local_peer());
   BOOST_REQUIRE(replaced);
   BOOST_REQUIRE_EQUAL(replaced->endpoints.size(), 2U);
   BOOST_TEST(std::ranges::none_of(replaced->endpoints,
                                   [](const auto& value) { return value.endpoint.transport.port == 4101U; }));
   BOOST_TEST(std::ranges::any_of(replaced->endpoints, [](const auto& value) {
      return value.endpoint.transport.port == 4099U && value.sources.learned;
   }));
   BOOST_TEST(std::ranges::any_of(replaced->endpoints, [](const auto& value) {
      return value.endpoint.transport.port == 4102U && value.sources.identify_unsigned;
   }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_merges_multiple_length_delimited_messages) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-multipart-server");
   const auto client_identity = make_test_certificate_identity("identify-multipart-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   const auto initial = wait_for_identified_peer(server, client.local_peer(), runtime, "multipart inbound Identify");
   auto advertised_endpoints = std::vector<endpoint>{};
   advertised_endpoints.reserve(48);
   for (auto port = std::uint16_t{4103}; port < 4151; ++port) {
      const auto host =
          "node-" + std::to_string(port) + "." + std::string(40, 'a') + "." + std::string(40, 'b') + ".example.test";
      advertised_endpoints.push_back(parse_endpoint("/dns4/" + host + "/udp/" + std::to_string(port) + "/quic-v1/p2p/" +
                                                    client.local_peer().to_string()));
   }
   const auto signed_record = make_signed_identify_peer_record(
       client_identity, advertised_endpoints, identify_peer_record_sequence(initial.signed_peer_record) + 1);

   auto first_protocols = std::vector<protocol_id>{builtins::ping};
   for (auto index = 0U; index < 9U; ++index) {
      first_protocols.push_back(protocol_id{
          .value = "/forge/identify-padding/" + std::to_string(index) + "/" + std::string(775, 'x'),
      });
   }

   const auto first = identify::document{
       .public_key = encode_public_key(public_key_for(client_identity)),
       .protocols = std::vector<protocol_id>{builtins::ping},
       .signed_peer_record = signed_record,
   };
   const auto second = identify::document{
       .protocol_version = "/forge/multipart/1",
       .agent_version = "forge-multipart/1",
       .listen_endpoints = std::vector<endpoint>{advertised_endpoints.front()},
       .protocols = std::move(first_protocols),
   };
   const auto first_bytes = identify::encode(first);
   const auto second_bytes = identify::encode(second);
   BOOST_REQUIRE(first_bytes.size() <= identify::limits{}.max_message_size);
   BOOST_REQUIRE(second_bytes.size() <= identify::limits{}.max_message_size);
   BOOST_REQUIRE(first_bytes.size() + second_bytes.size() > identify::limits{}.max_message_size);
   BOOST_REQUIRE(signed_record.size() > 4U * 1024U);
   auto encoded = wrap_length_delimited(first_bytes);
   const auto tail = wrap_length_delimited(second_bytes);
   encoded.insert(encoded.end(), tail.begin(), tail.end());

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   forge::asio::blocking::run(runtime, stream.async_write(encoded));
   forge::asio::blocking::run(runtime, stream.async_close());
   const auto found =
       wait_for_peer_record(server, client.local_peer(), runtime, "multipart Identify Push", [&](const auto& record) {
          return record.protocol_version == "/forge/multipart/1" && record.signed_peer_record == signed_record &&
                 std::ranges::any_of(record.protocols, [](const auto& value) {
                    return value.value.starts_with("/forge/identify-padding/");
                 });
       });

   BOOST_TEST(found.protocol_version == "/forge/multipart/1");
   BOOST_TEST(found.signed_peer_record == signed_record, boost::test_tools::per_element());
   BOOST_TEST(std::ranges::any_of(found.protocols, [](const auto& value) { return value == builtins::ping; }));
   BOOST_TEST(std::ranges::any_of(
       found.protocols, [](const auto& value) { return value.value.starts_with("/forge/identify-padding/"); }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_reset_does_not_publish_partial_document) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-reset-server");
   const auto client_identity = make_test_certificate_identity("identify-reset-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   const auto before = wait_for_identified_peer(server, client.local_peer(), runtime, "reset Push inbound Identify");
   const auto partial_protocol = protocol_id{.value = "/forge/identify-reset-must-not-publish/1"};
   const auto partial = identify::document{
       .protocol_version = "/forge/identify-reset/1",
       .protocols = std::vector<protocol_id>{builtins::ping, partial_protocol},
   };

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(partial))));
   wait_on_runtime(runtime, std::chrono::milliseconds{50}, "partial Identify Push delivery");
   stream.cancel();
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "partial Identify Push reset");

   const auto after = server.peers().find(client.local_peer());
   BOOST_REQUIRE(after.has_value());
   BOOST_TEST(after->protocol_version == before.protocol_version);
   BOOST_TEST(!std::ranges::contains(after->protocols, partial_protocol));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_preserves_fields_omitted_by_rust_partial_update) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-partial-server");
   const auto client_identity = make_test_certificate_identity("identify-partial-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(listen(client, runtime));
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   const auto before = wait_for_identified_peer(server, client.local_peer(), runtime, "partial Push inbound Identify");
   BOOST_REQUIRE(!before.protocol_version.empty());
   BOOST_REQUIRE(!before.agent_version.empty());
   BOOST_REQUIRE(!before.public_key.empty());
   BOOST_REQUIRE(!before.endpoints.empty());

   const auto product_protocol = protocol_id{.value = "/forge/rust-partial-push/1"};
   const auto partial = identify::document{
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify_push, product_protocol},
   };
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(partial))));
   forge::asio::blocking::run(runtime, stream.async_close());

   auto updated = std::optional<peer_store::record>{};
   for (auto attempt = 0U; attempt < 200U; ++attempt) {
      updated = server.peers().find(client.local_peer());
      if (updated && std::ranges::contains(updated->protocols, product_protocol)) {
         break;
      }
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "Rust partial Identify Push");
   }
   BOOST_REQUIRE(updated);
   BOOST_REQUIRE(std::ranges::contains(updated->protocols, product_protocol));
   BOOST_TEST(updated->protocol_version == before.protocol_version);
   BOOST_TEST(updated->agent_version == before.agent_version);
   BOOST_TEST(updated->public_key == before.public_key, boost::test_tools::per_element());
   BOOST_TEST(updated->signed_peer_record == before.signed_peer_record, boost::test_tools::per_element());
   BOOST_TEST(updated->endpoints.size() == before.endpoints.size());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_rejects_mismatched_endpoint_peer_suffix) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-push-mismatch-server");
   const auto client_identity = make_test_certificate_identity("identify-push-mismatch-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto bad_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4107/quic-v1/p2p/" + peer(215).to_string());
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto pushed = identify::document{
       .protocol_version = "/forge/push-bad-peer/1",
       .agent_version = "forge-push-bad-peer/1",
       .listen_endpoints = std::vector<endpoint>{bad_endpoint},
       .protocols = std::vector<protocol_id>{builtins::ping},
   };
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push mismatch propagation");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/push-bad-peer/1");
   BOOST_TEST(std::ranges::none_of(found->endpoints, [&](const peer_store::endpoint_record& record) {
      return record.endpoint.to_string() == bad_endpoint.to_string();
   }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_flushes_memory_peer_record_for_hydration) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-push-memory-server");
   const auto client_identity = make_test_certificate_identity("identify-push-memory-client");
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto server_options = options_for(server_identity);
   server_options.peer_state.persistence = persistence;
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto advertised = parse_endpoint("/ip4/127.0.0.1/udp/4201/quic-v1/p2p/" + client.local_peer().to_string());
   const auto encoded_public_key = encode_public_key(public_key_for(client_identity));
   const auto initial =
       wait_for_identified_peer(server, client.local_peer(), runtime, "persisted Push inbound Identify");
   const auto signed_peer_record =
       make_signed_identify_peer_record(client_identity, std::vector<endpoint>{advertised},
                                        identify_peer_record_sequence(initial.signed_peer_record) + 1);
   auto pushed = identify::document{
       .protocol_version = "/forge/push-persist/1",
       .agent_version = "forge-push-persist/1",
       .public_key = encoded_public_key,
       .listen_endpoints = std::vector<endpoint>{advertised},
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
       .signed_peer_record = signed_peer_record,
   };
   const auto decoded = identify::decode(identify::encode(pushed));
   BOOST_REQUIRE_EQUAL(decoded.listen_endpoints.size(), 1U);
   BOOST_REQUIRE(decoded.listen_endpoints.front().peer.has_value());
   BOOST_TEST(decoded.listen_endpoints.front().peer->to_bytes() == client.local_peer().to_bytes(),
              boost::test_tools::per_element());
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push persistence");
   forge::asio::blocking::run(runtime, server.peers().async_flush());

   auto hydrated = peer_store{peer_store::options{.persistence = persistence}};
   forge::asio::blocking::run(runtime, hydrated.async_hydrate());
   const auto snapshot = hydrated.snapshot(128);
   const auto found = std::ranges::find_if(
       snapshot, [](const peer_store::record& value) { return value.protocol_version == "/forge/push-persist/1"; });
   BOOST_REQUIRE(found != snapshot.end());
   BOOST_TEST(found->peer.to_bytes() == client_identity.peer.to_bytes(), boost::test_tools::per_element());
   BOOST_TEST(found->agent_version == "forge-push-persist/1");
   BOOST_TEST(found->public_key == encoded_public_key, boost::test_tools::per_element());
   BOOST_TEST(found->signed_peer_record == signed_peer_record, boost::test_tools::per_element());
   BOOST_TEST(
       std::ranges::any_of(found->protocols, [](const protocol_id& value) { return value == builtins::identify; }));
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.transport.port == 4201);
   BOOST_TEST(!found->endpoints.front().sources.learned);
   BOOST_TEST(found->endpoints.front().sources.identify_signed);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_accepts_rust_legacy_signed_peer_record) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-legacy-record-server");
   const auto client_identity = make_test_certificate_identity("identify-legacy-record-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto advertised = parse_endpoint("/ip4/127.0.0.1/udp/4207/quic-v1/p2p/" + client.local_peer().to_string());
   auto initial = std::optional<peer_store::record>{};
   for (auto attempt = 0U; attempt < 200U; ++attempt) {
      initial = server.peers().find(client.local_peer());
      if (initial && !initial->signed_peer_record.empty()) {
         break;
      }
      wait_on_runtime(runtime, std::chrono::milliseconds{5}, "inbound Identify before legacy Push");
   }
   BOOST_REQUIRE(initial);
   BOOST_REQUIRE(!initial->signed_peer_record.empty());
   const auto signed_peer_record =
       make_signed_rendezvous_peer_record(client_identity, std::vector<endpoint>{advertised},
                                          identify_peer_record_sequence(initial->signed_peer_record) + 1);
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto pushed = identify::document{
       .protocol_version = "/forge/rust-identify/1",
       .agent_version = "rust-libp2p",
       .public_key = encode_public_key(public_key_for(client_identity)),
       .listen_endpoints = std::vector<endpoint>{advertised},
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
       .signed_peer_record = signed_peer_record,
   };
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "Rust legacy Identify signed record");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/rust-identify/1");
   BOOST_TEST(found->signed_peer_record == signed_peer_record, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.transport.port == 4207U);
   BOOST_TEST(found->endpoints.front().sources.identify_signed);
   BOOST_TEST(
       std::ranges::any_of(found->protocols, [](const protocol_id& value) { return value == builtins::identify; }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_preserves_certified_record_across_unsigned_updates) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-certified-server");
   const auto client_identity = make_test_certificate_identity("identify-certified-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto advertised = parse_endpoint("/ip4/127.0.0.1/udp/4202/quic-v1/p2p/" + client.local_peer().to_string());
   const auto initial =
       wait_for_identified_peer(server, client.local_peer(), runtime, "certified Push inbound Identify");
   const auto signed_sequence = identify_peer_record_sequence(initial.signed_peer_record) + 1;
   const auto signed_record =
       make_signed_identify_peer_record(client_identity, std::vector<endpoint>{advertised}, signed_sequence);
   auto push = [&](identify::document document) {
      auto stream = forge::asio::blocking::run(
          runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
      forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(document))));
      forge::asio::blocking::run(runtime, stream.async_close());
      wait_on_runtime(runtime, std::chrono::milliseconds{100}, "Identify certified-record update");
   };

   push(identify::document{
       .protocol_version = "/forge/certified/1",
       .listen_endpoints = std::vector<endpoint>{advertised},
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify_push},
       .signed_peer_record = signed_record,
   });
   auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->signed_peer_record == signed_record, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().sources.identify_signed);

   const auto unsigned_advertised =
       parse_endpoint("/ip4/127.0.0.1/udp/4203/quic-v1/p2p/" + client.local_peer().to_string());
   push(identify::document{
       .protocol_version = "/forge/unsigned-update/1",
       .listen_endpoints = std::vector<endpoint>{unsigned_advertised},
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify_push},
   });
   found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/unsigned-update/1");
   BOOST_TEST(found->signed_peer_record == signed_record, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 2U);

   const auto equal_sequence_endpoint =
       parse_endpoint("/ip4/127.0.0.1/udp/4206/quic-v1/p2p/" + client.local_peer().to_string());
   const auto equal_sequence_record = make_signed_identify_peer_record(
       client_identity, std::vector<endpoint>{equal_sequence_endpoint}, signed_sequence);
   push(identify::document{
       .protocol_version = "/forge/equal-sequence-refresh/1",
       .listen_endpoints = std::vector<endpoint>{equal_sequence_endpoint},
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify_push},
       .signed_peer_record = equal_sequence_record,
   });
   found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/equal-sequence-refresh/1");
   BOOST_TEST(found->signed_peer_record == equal_sequence_record, boost::test_tools::per_element());
   BOOST_TEST(std::ranges::any_of(found->endpoints, [](const auto& value) {
      return value.endpoint.transport.port == 4206U && value.sources.identify_signed;
   }));

   const auto replacement = parse_endpoint("/ip4/127.0.0.1/udp/4204/quic-v1/p2p/" + client.local_peer().to_string());
   const auto ignored_unsigned =
       parse_endpoint("/ip4/127.0.0.1/udp/4205/quic-v1/p2p/" + client.local_peer().to_string());
   const auto replacement_record =
       make_signed_identify_peer_record(client_identity, std::vector<endpoint>{replacement}, signed_sequence + 1);
   push(identify::document{
       .protocol_version = "/forge/certified/2",
       .listen_endpoints = std::vector<endpoint>{ignored_unsigned},
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify_push},
       .signed_peer_record = replacement_record,
   });
   found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->signed_peer_record == replacement_record, boost::test_tools::per_element());
   BOOST_TEST(std::ranges::none_of(found->endpoints, [](const auto& value) {
      return value.endpoint.transport.port == 4202U || value.endpoint.transport.port == 4205U ||
             value.endpoint.transport.port == 4206U;
   }));
   BOOST_TEST(std::ranges::any_of(found->endpoints, [](const auto& value) {
      return value.endpoint.transport.port == 4204U && value.sources.identify_signed;
   }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_ignores_invalid_signed_record_without_dropping_authenticated_facts) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-invalid-record-server");
   const auto client_identity = make_test_certificate_identity("identify-invalid-record-client");
   const auto foreign_identity = make_test_certificate_identity("identify-invalid-record-foreign");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   const auto before = wait_for_identified_peer(server, client.local_peer(), runtime, "invalid Push inbound Identify");
   auto invalid_record = make_signed_identify_peer_record(foreign_identity);
   auto pushed = identify::document{
       .protocol_version = "/forge/invalid-record/1",
       .protocols = std::vector<protocol_id>{builtins::ping},
       .signed_peer_record = std::move(invalid_record),
   };
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "Identify invalid signed record rejection");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/invalid-record/1");
   BOOST_TEST(found->signed_peer_record == before.signed_peer_record, boost::test_tools::per_element());
   BOOST_TEST(std::ranges::any_of(found->protocols, [](const auto& value) { return value == builtins::ping; }));
   BOOST_TEST(forge::asio::blocking::run(runtime, client.async_ping(server.local_peer())).count() >= 0);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_rejects_canonical_record_with_mismatched_inner_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-inner-peer-server");
   const auto client_identity = make_test_certificate_identity("identify-inner-peer-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   const auto before =
       wait_for_identified_peer(server, client.local_peer(), runtime, "inner-peer Push inbound Identify");
   const auto advertised = parse_endpoint("/ip4/127.0.0.1/udp/4208/quic-v1/p2p/" + client.local_peer().to_string());
   const auto payload = rendezvous::codec::encode_peer_record(rendezvous::peer_record{
       .peer = peer(214),
       .endpoints = std::vector<endpoint>{advertised},
       .sequence = identify_peer_record_sequence(before.signed_peer_record) + 1,
   });
   const auto invalid_record =
       signed_envelope::seal(public_key_for(client_identity),
                             forge::crypto::pki::pem::read_private_key(client_identity.private_key_pem),
                             "libp2p-peer-record", identify_peer_record_payload_type(), payload)
           .encode();
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   const auto pushed = identify::document{
       .protocol_version = "/forge/inner-peer-mismatch/1",
       .listen_endpoints = std::vector<endpoint>{advertised},
       .protocols = std::vector<protocol_id>{builtins::ping},
       .signed_peer_record = invalid_record,
   };
   forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   forge::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "Identify inner peer mismatch");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/inner-peer-mismatch/1");
   BOOST_TEST(found->signed_peer_record == before.signed_peer_record, boost::test_tools::per_element());
   BOOST_TEST(std::ranges::any_of(found->endpoints, [](const auto& value) {
      return value.endpoint.transport.port == 4208U && value.sources.identify_unsigned;
   }));

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_read_timeout_releases_inbound_stream) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-push-timeout-server");
   const auto client_identity = make_test_certificate_identity("identify-push-timeout-client");
   auto server_options = options_for(server_identity);
   server_options.identify.timeout = std::chrono::milliseconds{50};
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()})));
   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));

   auto released = false;
   for (auto attempt = 0U; attempt < 100U && !released; ++attempt) {
      released = server.diagnostics().resources.active_streams == 0U;
      if (!released) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "Identify Push read timeout");
      }
   }
   BOOST_TEST(released);
   BOOST_TEST(forge::asio::blocking::run(runtime, client.async_ping(server.local_peer())).count() >= 0);
   stream.cancel();

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_unsupported_protocol_rejection_keeps_session_usable) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(79))};
   auto client = node{runtime, options_for(peer(80))};

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_open_protocol_stream(server.local_peer(), protocol_id{.value = "/product/missing/1"}));
      BOOST_FAIL("expected unsupported protocol rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::unsupported_protocol));
   }

   auto negotiation_streams_closed = false;
   for (auto attempt = 0U; attempt < 100U && !negotiation_streams_closed; ++attempt) {
      negotiation_streams_closed =
          client.diagnostics().resources.active_streams == 0U && server.diagnostics().resources.active_streams == 0U;
      if (!negotiation_streams_closed) {
         wait_on_runtime(runtime, std::chrono::milliseconds{5}, "unsupported protocol stream reset");
      }
   }
   BOOST_REQUIRE(negotiation_streams_closed);

   auto stream =
       forge::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::ping));
   const auto payload = std::vector<std::uint8_t>(32, 0x24);
   forge::asio::blocking::run(runtime, stream.async_write(payload));
   BOOST_TEST(forge::asio::blocking::run(runtime, stream.async_read()) == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_cached_protocol_open_shutdown_does_not_penalize_peer) {
   auto server_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto client_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{server_runtime, options_for(peer(207))};
   auto client = node{client_runtime, options_for(peer(208))};
   register_echo(server);

   const auto server_endpoint = listen(server, server_runtime);
   (void)forge::asio::blocking::run(
       client_runtime,
       client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   const auto before = client.peers().find(server.local_peer());
   BOOST_REQUIRE(before.has_value());
   const auto endpoint_before = std::ranges::find_if(before->endpoints, [&](const auto& current) {
      return current.endpoint.to_string() == server_endpoint.to_string();
   });
   BOOST_REQUIRE(endpoint_before != before->endpoints.end());
   const auto peer_failures_before = before->failures;
   const auto endpoint_failures_before = endpoint_before->failures;
   const auto endpoint_backoff_before = endpoint_before->backoff_until;
   const auto direct_failures_before = client.metrics().direct_failures;

   auto release_server = block_runtime(server_runtime, "cached protocol shutdown barrier");
   const auto attempts_before = client.metrics().path_direct_attempts;
   auto opened = boost::asio::co_spawn(
       client_runtime.context(),
       client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                         node::open_options{.allow_relay = false,
                                                            .timeout = std::chrono::seconds{5},
                                                            .direct_attempt_timeout = std::chrono::seconds{5},
                                                            .max_direct_endpoints = 1}),
       boost::asio::use_future);
   const auto attempt_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (client.metrics().path_direct_attempts == attempts_before) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < attempt_deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }
   auto stopped = boost::asio::co_spawn(client_runtime.context(), client.async_stop(), boost::asio::use_future);
   const auto stopped_without_remote_progress = stopped.wait_for(std::chrono::seconds{2}) == std::future_status::ready;
   release_server->set_value();
   BOOST_REQUIRE(stopped_without_remote_progress);
   stopped.get();
   BOOST_REQUIRE(opened.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   try {
      static_cast<void>(opened.get());
      BOOST_FAIL("expected cached protocol open shutdown");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      const auto code = *exceptions::code_of(error);
      BOOST_CHECK(code == exceptions::code::canceled || code == exceptions::code::closed);
   }

   const auto after = client.peers().find(server.local_peer());
   BOOST_REQUIRE(after.has_value());
   const auto endpoint_after = std::ranges::find_if(after->endpoints, [&](const auto& current) {
      return current.endpoint.to_string() == server_endpoint.to_string();
   });
   BOOST_REQUIRE(endpoint_after != after->endpoints.end());
   BOOST_TEST(after->failures == peer_failures_before);
   BOOST_TEST(endpoint_after->failures == endpoint_failures_before);
   const auto backoff_unchanged = endpoint_after->backoff_until == endpoint_backoff_before;
   BOOST_TEST(backoff_unchanged);
   BOOST_TEST(client.metrics().direct_failures == direct_failures_before);

   forge::asio::blocking::run(server_runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_cached_protocol_timeout_penalizes_peer_without_advancing_dht_failure) {
   auto server_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto client_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto limits = dht::options{};
   limits.failure_threshold = 1;
   const auto server_identity = make_test_certificate_identity("cached-protocol-server");
   const auto client_identity = make_test_certificate_identity("cached-protocol-client");
   auto server_options = dht_options_for(server_identity, custom_test_dht_profile(dht::mode::server, limits));
   auto client_options = dht_options_for(client_identity, custom_test_dht_profile(dht::mode::client, limits));
   client_options.limits.discovery.rendezvous_enabled = false;
   auto server = node{server_runtime, std::move(server_options)};
   auto client = node{client_runtime, std::move(client_options)};
   register_echo(server);

   const auto server_endpoint = listen(server, server_runtime);
   verify_dht_server(client_runtime, client, server, server_endpoint, content_swarm_test_dht);
   const auto routing_before = client.routing_status(content_swarm_test_dht);
   const auto before = client.peers().find(server.local_peer());
   BOOST_REQUIRE(before.has_value());
   const auto endpoint_before = std::ranges::find_if(before->endpoints, [&](const auto& current) {
      return current.endpoint.to_string() == server_endpoint.to_string();
   });
   BOOST_REQUIRE(endpoint_before != before->endpoints.end());
   const auto peer_failures_before = before->failures;
   const auto endpoint_failures_before = endpoint_before->failures;
   const auto direct_failures_before = client.metrics().direct_failures;

   auto release_server = block_runtime(server_runtime, "cached protocol timeout barrier");
   try {
      static_cast<void>(forge::asio::blocking::run(
          client_runtime,
          client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                            node::open_options{.allow_relay = false,
                                                               .timeout = std::chrono::milliseconds{150},
                                                               .direct_attempt_timeout = std::chrono::milliseconds{150},
                                                               .max_direct_endpoints = 1})));
      BOOST_FAIL("expected cached protocol open timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_CHECK(*exceptions::code_of(error) == exceptions::code::timeout);
   }

   const auto after = client.peers().find(server.local_peer());
   BOOST_REQUIRE(after.has_value());
   const auto endpoint_after = std::ranges::find_if(after->endpoints, [&](const auto& current) {
      return current.endpoint.to_string() == server_endpoint.to_string();
   });
   BOOST_REQUIRE(endpoint_after != after->endpoints.end());
   BOOST_TEST(after->failures == peer_failures_before + 1U);
   BOOST_TEST(endpoint_after->failures == endpoint_failures_before + 1U);
   const auto backoff_applied = endpoint_after->backoff_until > std::chrono::system_clock::now();
   BOOST_TEST(backoff_applied);
   BOOST_TEST(client.metrics().direct_failures == direct_failures_before + 1U);
   BOOST_TEST(client.routing_status(content_swarm_test_dht).active == routing_before.active);

   release_server->set_value();
   forge::asio::blocking::run(client_runtime, client.async_stop());
   forge::asio::blocking::run(server_runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_stop_before_direct_path_deadline_does_not_penalize_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(211))};
   auto accepted = std::make_shared<std::promise<void>>();
   auto accepted_future = accepted->get_future();
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime, std::chrono::seconds{2}, accepted);
   const auto stalled_peer = peer(212);
   client.peers().learn_endpoint(stalled_peer, stalled_endpoint, capability_set{.bits = capabilities::direct_quic});
   const auto before = client.peers().find(stalled_peer);
   BOOST_REQUIRE(before.has_value());
   BOOST_REQUIRE(!before->endpoints.empty());
   const auto peer_failures_before = before->failures;
   const auto endpoint_failures_before = before->endpoints.front().failures;
   const auto endpoint_backoff_before = before->endpoints.front().backoff_until;
   const auto direct_failures_before = client.metrics().direct_failures;

   auto opened = boost::asio::co_spawn(
       runtime.context(),
       client.async_open_protocol_stream(stalled_peer, builtins::echo,
                                         node::open_options{.allow_relay = false,
                                                            .timeout = std::chrono::milliseconds{150},
                                                            .direct_attempt_timeout = std::chrono::milliseconds{150},
                                                            .max_direct_endpoints = 1}),
       boost::asio::use_future);
   wait_for_server(accepted_future, std::chrono::seconds{2}, "direct path shutdown admission");
   auto release_runtime = block_runtime(runtime, "direct path shutdown barrier");
   client.stop();
   std::this_thread::sleep_for(std::chrono::milliseconds{250});
   release_runtime->set_value();

   BOOST_REQUIRE(opened.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   try {
      static_cast<void>(opened.get());
      BOOST_FAIL("expected direct path shutdown");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      const auto code = *exceptions::code_of(error);
      BOOST_CHECK_MESSAGE(code == exceptions::code::canceled || code == exceptions::code::closed,
                          "unexpected shutdown code " << static_cast<int>(code));
   }
   forge::asio::blocking::run(runtime, client.async_stop());

   const auto after = client.peers().find(stalled_peer);
   BOOST_REQUIRE(after.has_value());
   BOOST_REQUIRE(!after->endpoints.empty());
   BOOST_TEST(after->failures == peer_failures_before);
   BOOST_TEST(after->endpoints.front().failures == endpoint_failures_before);
   const auto backoff_unchanged = after->endpoints.front().backoff_until == endpoint_backoff_before;
   BOOST_TEST(backoff_unchanged);
   BOOST_TEST(client.metrics().direct_failures == direct_failures_before);
}

BOOST_AUTO_TEST_CASE(p2p_direct_path_timeout_before_stop_penalizes_peer) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(peer(213))};
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime);
   const auto stalled_peer = peer(214);
   client.peers().learn_endpoint(stalled_peer, stalled_endpoint, capability_set{.bits = capabilities::direct_quic});
   const auto before = client.peers().find(stalled_peer);
   BOOST_REQUIRE(before.has_value());
   BOOST_REQUIRE(!before->endpoints.empty());
   const auto peer_failures_before = before->failures;
   const auto endpoint_failures_before = before->endpoints.front().failures;
   const auto direct_failures_before = client.metrics().direct_failures;

   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime,
          client.async_open_protocol_stream(stalled_peer, builtins::echo,
                                            node::open_options{.allow_relay = false,
                                                               .timeout = std::chrono::milliseconds{100},
                                                               .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                               .max_direct_endpoints = 1})));
      BOOST_FAIL("expected direct path timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_REQUIRE(exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*exceptions::code_of(error)) == static_cast<int>(exceptions::code::timeout));
   }
   client.stop();

   const auto after = client.peers().find(stalled_peer);
   BOOST_REQUIRE(after.has_value());
   BOOST_REQUIRE(!after->endpoints.empty());
   BOOST_TEST(after->failures > peer_failures_before);
   BOOST_TEST(after->endpoints.front().failures > endpoint_failures_before);
   BOOST_TEST(after->endpoints.front().backoff_until > std::chrono::system_clock::now());
   BOOST_TEST(client.metrics().direct_failures > direct_failures_before);
   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_path_manager_tries_next_direct_endpoint_after_attempt_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(64))};
   auto client = node{runtime, options_for(peer(65))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), make_quic_endpoint(9),
                                 capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = false,
                                                      .timeout = std::chrono::milliseconds{2'000},
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .max_direct_endpoints = 2,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'d', 'i', 'r', 'e', 'c', 't'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_attempts >= 2U);
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);
   auto record = client.peers().find(server.local_peer());
   BOOST_REQUIRE(record.has_value());
   auto failed = std::ranges::find_if(record->endpoints, [&](const peer_store::endpoint_record& current) {
      return current.endpoint.to_string() == make_quic_endpoint(9).to_string();
   });
   auto succeeded = std::ranges::find_if(record->endpoints, [&](const peer_store::endpoint_record& current) {
      return current.endpoint.to_string() == server_endpoint.to_string();
   });
   BOOST_REQUIRE(failed != record->endpoints.end());
   BOOST_REQUIRE(succeeded != record->endpoints.end());
   BOOST_TEST(failed->failures >= 1U);
   BOOST_TEST(failed->backoff_until > std::chrono::system_clock::now());
   BOOST_TEST(succeeded->successes >= 1U);
   BOOST_TEST(succeeded->backoff_until == std::chrono::system_clock::time_point{});

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_path_manager_tries_next_tcp_endpoint_after_upgrade_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(205))};
   auto client = node{runtime, options_for(peer(206))};
   register_echo(server);

   const auto stalled_endpoint = start_stalling_tcp_peer(runtime);
   const auto server_endpoint = listen_tcp(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), stalled_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = false,
                                                      .timeout = std::chrono::milliseconds{2'000},
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .max_direct_endpoints = 2,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'t', 'c', 'p', '-', 'd', 'e', 'a', 'd'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_attempts >= 2U);
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_successful_connect_deadline_does_not_poison_session) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(41))};
   auto client = node{runtime, options_for(peer(42))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)forge::asio::blocking::run(
       runtime,
       client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer(),
                                                                   .timeout = std::chrono::milliseconds{500}}));
   wait_on_runtime(runtime, std::chrono::milliseconds{700}, "post-connect deadline grace");

   auto stream = forge::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{.timeout = std::chrono::milliseconds{1'000}}));
   const auto payload = std::vector<std::uint8_t>{'d', 'e', 'a', 'd', 'l', 'i', 'n', 'e'};
   forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = forge::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   forge::asio::blocking::run(runtime, client.async_stop());
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_duplicate_protocol_handler_is_rejected) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(3))};
   register_echo(value);

   try {
      register_echo(value);
      BOOST_FAIL("expected duplicate protocol handler rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::duplicate_protocol));
   }
}

BOOST_AUTO_TEST_CASE(p2p_product_message_packs_typed_payload_as_data) {
   const auto protocol = protocol_id{.value = "/product/chunk-announce/1"};
   const auto value = product_announce{.ref = "chunk-1"};

   const auto message = forge::net::p2p::message{protocol, value};

   BOOST_TEST(message.protocol().value == protocol.value);
   BOOST_TEST(message.codec().value == "forge.raw");
   BOOST_TEST(message.as<product_announce>().ref == value.ref);
   BOOST_TEST(!message.data().empty());
}

BOOST_AUTO_TEST_CASE(p2p_connect_rejects_non_positive_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(32))};

   try {
      (void)forge::asio::blocking::run(
          runtime,
          client.async_connect(make_quic_endpoint(9), node::connect_options{.expected_peer = peer(33),
                                                                            .timeout = std::chrono::milliseconds{0}}));
      BOOST_FAIL("expected invalid connect timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }

   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_open_protocol_rejects_non_positive_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(39))};

   try {
      (void)forge::asio::blocking::run(
          runtime, client.async_open_protocol_stream(peer(40), builtins::echo,
                                                     node::open_options{.timeout = std::chrono::milliseconds{0}}));
      BOOST_FAIL("expected invalid protocol open timeout");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }

   forge::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_expires_stale_reachability_observation) {
   auto store = peer_store{};
   store.upsert(peer_store::record{
       .peer = peer(70),
       .capabilities = capability_set{.bits = capabilities::direct_quic},
       .reachability = reachability::state::publicly_reachable,
       .observed_endpoint = make_quic_endpoint(12345),
       .reachability_expires_at = std::chrono::system_clock::now() - std::chrono::seconds{1},
   });

   const auto record = store.find(peer(70));
   BOOST_REQUIRE(record.has_value());
   BOOST_TEST(static_cast<int>(record->reachability) == static_cast<int>(reachability::state::unknown));
   BOOST_TEST(!record->observed_endpoint.has_value());

   const auto snapshot = store.snapshot(1);
   BOOST_REQUIRE_EQUAL(snapshot.size(), 1U);
   BOOST_TEST(static_cast<int>(snapshot.front().reachability) == static_cast<int>(reachability::state::unknown));
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_identify_update_preserves_independent_endpoint_sources) {
   auto store = peer_store{};
   const auto remote = peer(223);
   const auto first = parse_endpoint("/ip4/127.0.0.1/udp/4223/quic-v1/p2p/" + remote.to_string());
   const auto second = parse_endpoint("/ip4/127.0.0.1/udp/4224/quic-v1/p2p/" + remote.to_string());
   store.learn_endpoint(remote, first);

   const auto updated = store.apply_identify(remote, peer_store::identify_update{
                                                         .protocol_version = "/forge/atomic-identify/1",
                                                         .protocols = std::vector<protocol_id>{builtins::identify},
                                                         .capabilities = capability_set{},
                                                         .unsigned_endpoints = std::vector<endpoint>{second},
                                                     });

   const auto found = store.find(remote);
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/forge/atomic-identify/1");
   BOOST_TEST(updated.protocol_version == found->protocol_version);
   BOOST_TEST(std::ranges::any_of(found->endpoints,
                                  [&](const auto& value) { return value.endpoint.to_string() == first.to_string(); }));
   const auto identified = std::ranges::find_if(
       found->endpoints, [&](const auto& value) { return value.endpoint.to_string() == second.to_string(); });
   BOOST_REQUIRE(identified != found->endpoints.end());
   BOOST_TEST(identified->sources.identify_unsigned);
   BOOST_TEST(!identified->sources.learned);

   const auto third = parse_endpoint("/ip4/127.0.0.1/udp/4225/quic-v1/p2p/" + remote.to_string());
   store.learn_endpoint(remote, third);
   const auto observed_at = std::chrono::system_clock::now();
   const auto discovered = store.apply_discovery(remote, peer_store::discovery_update{
                                                             .source = discovery::source::rendezvous,
                                                             .observed_at = observed_at,
                                                             .expires_at = observed_at + std::chrono::minutes{5},
                                                         });
   BOOST_REQUIRE(discovered);
   BOOST_TEST(static_cast<int>(discovered->discovered_by) == static_cast<int>(discovery::source::rendezvous));
   BOOST_TEST(discovered->discovered_at == observed_at);
   BOOST_TEST(std::ranges::any_of(discovered->endpoints,
                                  [&](const auto& value) { return value.endpoint.to_string() == third.to_string(); }));

   store.upsert_relay_reservation(peer_store::relay_record{
       .relay = remote,
       .reservation_id = 17,
       .expires_at = observed_at + std::chrono::minutes{2},
       .endpoints = {third},
   });
   const auto with_relay = store.find(remote);
   BOOST_REQUIRE(with_relay);
   BOOST_TEST(with_relay->protocol_version == "/forge/atomic-identify/1");
   BOOST_TEST(std::ranges::any_of(with_relay->endpoints,
                                  [&](const auto& value) { return value.endpoint.to_string() == first.to_string(); }));
   BOOST_REQUIRE_EQUAL(with_relay->relay_reservations.size(), 1U);
   BOOST_TEST(with_relay->relay_reservations.front().reservation_id == 17U);
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_preserves_identify_discovery_and_relay_state) {
   auto store = peer_store{};
   const auto remote_identity = make_test_identity();
   const auto remote = remote_identity.peer;
   const auto public_key_bytes = encode_public_key(remote_identity.key);
   const auto now = std::chrono::system_clock::now();
   const auto original_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4227/quic-v1/p2p/" + remote.to_string());
   const auto exchanged_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4228/quic-v1/p2p/" + remote.to_string());
   store.upsert(peer_store::record{
       .peer = remote,
       .capabilities = capability_set{.bits = capabilities::direct_quic},
       .discovered_by = discovery::source::rendezvous,
       .protocol_version = "/ipfs/id/1.0.0",
       .agent_version = "forge-preserved",
       .public_key = public_key_bytes,
       .protocols = {builtins::identify},
       .signed_peer_record = {0x03, 0x04},
       .endpoints = {peer_store::endpoint_record{.endpoint = original_endpoint}},
       .relay_reservations = {peer_store::relay_record{
           .relay = remote,
           .reservation_id = 23,
           .expires_at = now + std::chrono::minutes{5},
       }},
       .discovered_at = now,
       .discovery_expires_at = now + std::chrono::minutes{10},
   });

   detail::learn_authenticated_peer_exchange_response(
       store,
       peer_exchange_message{
           .kind = peer_exchange_message::type::peer_exchange_response,
           .peer = remote,
           .capabilities = capability_set{.bits = capabilities::peer_exchange},
           .endpoints = {peer_exchange_message::endpoint_record{
               .peer = remote,
               .endpoint = exchanged_endpoint,
               .capabilities = capability_set{.bits = capabilities::pubsub},
           }},
       },
       remote, exchanged_endpoint);

   const auto stored = store.find(remote);
   BOOST_REQUIRE(stored);
   BOOST_TEST(stored->protocol_version == "/ipfs/id/1.0.0");
   BOOST_TEST(stored->agent_version == "forge-preserved");
   BOOST_TEST(stored->public_key == public_key_bytes);
   BOOST_TEST(stored->signed_peer_record == (std::vector<std::uint8_t>{0x03, 0x04}));
   BOOST_TEST(static_cast<int>(stored->discovered_by) == static_cast<int>(discovery::source::rendezvous));
   BOOST_TEST(stored->discovered_at == now);
   BOOST_REQUIRE_EQUAL(stored->relay_reservations.size(), 1U);
   BOOST_TEST(stored->relay_reservations.front().reservation_id == 23U);
   BOOST_TEST(stored->capabilities.has(capabilities::direct_quic));
   BOOST_TEST(stored->capabilities.has(capabilities::peer_exchange));
   BOOST_TEST(stored->capabilities.has(capabilities::pubsub));
   BOOST_TEST(std::ranges::any_of(stored->endpoints, [&](const auto& value) {
      return value.endpoint.to_string() == original_endpoint.to_string();
   }));
   BOOST_TEST(std::ranges::any_of(stored->endpoints, [&](const auto& value) {
      return value.endpoint.to_string() == exchanged_endpoint.to_string();
   }));
}

BOOST_AUTO_TEST_CASE(p2p_relay_maintenance_preserves_concurrent_peer_facts) {
   auto store = peer_store{};
   const auto remote = peer(228);
   const auto now = std::chrono::system_clock::now();
   for (auto round = 0U; round < 64U; ++round) {
      const auto identified_agent = "forge-concurrent-identify-" + std::to_string(round);
      store.upsert(peer_store::record{
          .peer = remote,
          .protocol_version = "/ipfs/id/1.0.0",
          .agent_version = "forge-before-maintenance",
          .protocols = {builtins::identify},
          .relay_reservations =
              {
                  peer_store::relay_record{
                      .relay = remote, .reservation_id = 31, .expires_at = now - std::chrono::seconds{1}},
                  peer_store::relay_record{
                      .relay = remote, .reservation_id = 32, .expires_at = now + std::chrono::minutes{5}},
              },
      });

      auto start_promise = std::promise<void>{};
      auto start = start_promise.get_future().share();
      auto backoff = std::async(std::launch::async, [&] {
         start.wait();
         relay_discovery::backoff_candidate(store, remote, now + std::chrono::seconds{30});
      });
      auto identify = std::async(std::launch::async, [&] {
         start.wait();
         static_cast<void>(
             store.apply_identify(remote, peer_store::identify_update{.agent_version = identified_agent}));
      });
      auto prune = std::async(std::launch::async, [&] {
         start.wait();
         relay_discovery::prune_expired_reservations(store, now, 1);
      });
      start_promise.set_value();
      backoff.get();
      identify.get();
      prune.get();

      const auto stored = store.find(remote);
      BOOST_REQUIRE(stored);
      BOOST_TEST(stored->protocol_version == "/ipfs/id/1.0.0");
      BOOST_TEST(stored->agent_version == identified_agent);
      BOOST_TEST(stored->failures == 1U);
      BOOST_TEST(stored->discovery_backoff_until == now + std::chrono::seconds{30});
      BOOST_REQUIRE_EQUAL(stored->relay_reservations.size(), 1U);
      BOOST_TEST(stored->relay_reservations.front().reservation_id == 32U);
   }
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_bounds_variable_peer_record_state) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto subject = peer(70);
   auto store = peer_store{peer_store::options{
       .max_endpoints_per_peer = 1,
       .max_protocols_per_peer = 1,
       .max_relay_reservations_per_peer = 1,
       .max_relay_endpoints_per_reservation = 1,
       .max_peer_record_bytes = 128,
   }};

   store.learn_endpoint(subject, make_quic_endpoint(4070));
   BOOST_CHECK_THROW(store.learn_endpoint(subject, make_quic_endpoint(4071)), exceptions::backpressure_rejected);
   const auto stored = store.find(subject);
   BOOST_REQUIRE(stored.has_value());
   BOOST_REQUIRE_EQUAL(stored->endpoints.size(), 1U);
   BOOST_TEST(stored->endpoints.front().endpoint.to_string() == make_quic_endpoint(4070).to_string());

   BOOST_CHECK_THROW(store.upsert(peer_store::record{
                         .peer = peer(71),
                         .protocols = std::vector<protocol_id>{{.value = "/forge/test/1"}, {.value = "/forge/test/2"}},
                     }),
                     exceptions::backpressure_rejected);
   BOOST_CHECK_THROW(store.upsert(peer_store::record{
                         .peer = peer(72),
                         .relay_reservations = std::vector<peer_store::relay_record>{peer_store::relay_record{
                             .relay = peer(73),
                             .endpoints = std::vector<endpoint>{make_quic_endpoint(4072), make_quic_endpoint(4073)},
                         }},
                     }),
                     exceptions::backpressure_rejected);
   BOOST_CHECK_THROW(store.upsert(peer_store::record{
                         .peer = peer(74),
                         .agent_version = std::string(129, 'x'),
                     }),
                     exceptions::backpressure_rejected);
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                                .namespace_name = "forge.bounds",
                                                .peer = peer(77),
                                                .signed_peer_record = std::vector<std::uint8_t>(129, 0x32U),
                                            }))),
       exceptions::backpressure_rejected);
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(
           runtime, store.async_upsert_rendezvous(rendezvous::registration{
                        .namespace_name = "forge.bounds",
                        .peer = peer(78),
                        .endpoints = std::vector<endpoint>{make_quic_endpoint(4076), make_quic_endpoint(4077)},
                    }))),
       exceptions::backpressure_rejected);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_rejects_oversized_hydration_page_atomically) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = peer_store::make_memory_persistence();
   forge::asio::blocking::run(
       runtime,
       persistence->async_apply(peer_store::mutation_batch{
           .peer_upserts =
               std::vector<peer_store::record>{
                   peer_store::record{.peer = peer(75)},
                   peer_store::record{
                       .peer = peer(76),
                       .protocols = std::vector<protocol_id>{{.value = "/forge/test/1"}, {.value = "/forge/test/2"}},
                   },
               },
       }));
   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_protocols_per_peer = 1,
   }};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, store.async_hydrate()), exceptions::backpressure_rejected);
   BOOST_TEST(store.snapshot(10).empty());
   BOOST_TEST(store.persistence_state().degraded);

   forge::asio::blocking::run(runtime, store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_rejects_oversized_rendezvous_hydration_atomically) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto rendezvous_persistence = peer_store::make_memory_persistence();
   (void)forge::asio::blocking::run(
       runtime, rendezvous_persistence->async_apply(peer_store::mutation_batch{
                    .rendezvous_upserts = std::vector<rendezvous::registration>{rendezvous::registration{
                        .namespace_name = "forge.bounds",
                        .peer = peer(80),
                        .signed_peer_record = std::vector<std::uint8_t>(129, 0x42U),
                        .sequence = 1,
                    }},
                    .rendezvous_sequence_high_watermark = 1,
                }));
   auto rendezvous_store = peer_store{peer_store::options{
       .persistence = rendezvous_persistence,
       .max_peer_record_bytes = 128,
   }};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, rendezvous_store.async_hydrate()),
                     exceptions::backpressure_rejected);
   BOOST_TEST(rendezvous_store.discover_rendezvous("forge.bounds", 0, 1).empty());
   forge::asio::blocking::run(runtime, rendezvous_store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_memory_persistence_hydrates_bounded_pages) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};
   auto batch = peer_store::mutation_batch{};
   for (auto index = std::uint8_t{}; index < 5; ++index) {
      batch.peer_upserts.push_back(peer_store::record{
          .peer = peer(static_cast<std::uint8_t>(110 + index)),
          .protocol_version = "/forge/hydrated/1",
          .discovery_expires_at = expires_at,
      });
   }
   for (auto index = std::uint8_t{}; index < 3; ++index) {
      batch.rendezvous_upserts.push_back(rendezvous::registration{
          .namespace_name = "forge.paged",
          .peer = peer(static_cast<std::uint8_t>(130 + index)),
          .ttl = std::chrono::hours{1},
          .expires_at = expires_at,
          .sequence = static_cast<std::uint64_t>(index + 1),
      });
   }
   forge::asio::blocking::run(runtime, persistence->async_apply(std::move(batch)));

   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_peers = 5,
       .max_rendezvous = 3,
       .max_pending = 5,
       .hydration_page_limit = 2,
       .prune_page_limit = 2,
   }};
   forge::asio::blocking::run(runtime, store.async_hydrate());

   BOOST_REQUIRE_EQUAL(store.snapshot(10).size(), 5U);
   BOOST_REQUIRE_EQUAL(store.discover_rendezvous("forge.paged", 0, 2).size(), 2U);
   BOOST_TEST(std::ranges::all_of(persistence->hydration_requests,
                                  [](const auto& request) { return request.limit > 0 && request.limit <= 2; }));
   BOOST_TEST(std::ranges::count_if(persistence->hydration_requests, [](const auto& request) {
                 return request.kind == peer_store::hydration_kind::peers;
              }) == 3U);
   BOOST_TEST(std::ranges::count_if(persistence->hydration_requests, [](const auto& request) {
                 return request.kind == peer_store::hydration_kind::rendezvous;
              }) == 2U);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_memory_persistence_paginates_deterministically) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = peer_store::make_memory_persistence();
   auto batch = peer_store::mutation_batch{};
   for (const auto value : std::vector<std::uint8_t>{175, 171, 174, 172, 173}) {
      batch.peer_upserts.push_back(peer_store::record{.peer = peer(value)});
   }
   forge::asio::blocking::run(runtime, persistence->async_apply(std::move(batch)));

   auto hydrated = std::vector<peer_id>{};
   auto cursor = std::optional<std::vector<std::byte>>{};
   do {
      auto page = forge::asio::blocking::run(runtime, persistence->async_hydrate(peer_store::hydration_request{
                                                          .kind = peer_store::hydration_kind::peers,
                                                          .cursor = cursor,
                                                          .limit = 2,
                                                      }));
      BOOST_TEST(page.peers.size() <= 2U);
      for (const auto& value : page.peers) {
         hydrated.push_back(value.peer);
      }
      cursor = std::move(page.cursor);
   } while (cursor);

   BOOST_REQUIRE_EQUAL(hydrated.size(), 5U);
   BOOST_TEST(std::ranges::is_sorted(hydrated));
   const auto unique = std::set<peer_id>{hydrated.begin(), hydrated.end()};
   BOOST_TEST(unique.size() == hydrated.size());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_hydration_durably_removes_peer_displaced_by_concurrent_mutation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->block_hydrate = true;
   persistence->retain_delegate_on_close = true;
   const auto durable_peer = peer(184);
   const auto concurrent_peer = peer(185);
   auto initial = peer_store::mutation_batch{};
   initial.peer_upserts.push_back(peer_store::record{.peer = durable_peer, .protocol_version = "/forge/old/1"});
   forge::asio::blocking::run(runtime, persistence->async_apply(std::move(initial)));

   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_peers = 1,
       .max_pending = 2,
   }};
   auto hydration = boost::asio::co_spawn(runtime.context(), store.async_hydrate(), boost::asio::use_future);
   BOOST_REQUIRE(persistence->wait_until_hydrate_blocked());
   store.upsert(peer_store::record{.peer = concurrent_peer, .protocol_version = "/forge/new/1"});
   persistence->release_hydrate();
   hydration.get();
   persistence->block_hydrate = false;
   forge::asio::blocking::run(runtime, store.async_flush());

   auto reopened = peer_store{peer_store::options{
       .persistence = persistence,
       .max_peers = 1,
       .max_pending = 2,
   }};
   forge::asio::blocking::run(runtime, reopened.async_hydrate());
   BOOST_REQUIRE_EQUAL(reopened.snapshot(10).size(), 1U);
   BOOST_TEST(reopened.find(concurrent_peer).has_value());
   BOOST_TEST(!reopened.find(durable_peer).has_value());
   BOOST_TEST(persistence->applied_peer_removals == 1U);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_rendezvous_high_watermark_survives_deleted_highest_registration) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->retain_delegate_on_close = true;
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};
   auto highest_sequence = std::uint64_t{};

   {
      auto store = peer_store{peer_store::options{.persistence = persistence}};
      forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                              .namespace_name = "forge.sequence",
                                              .peer = peer(176),
                                              .ttl = std::chrono::hours{1},
                                              .expires_at = expires_at,
                                          }));
      forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                              .namespace_name = "forge.sequence",
                                              .peer = peer(177),
                                              .ttl = std::chrono::hours{1},
                                              .expires_at = expires_at,
                                          }));
      const auto registrations = store.discover_rendezvous("forge.sequence", 0, 10);
      BOOST_REQUIRE_EQUAL(registrations.size(), 2U);
      highest_sequence = registrations.back().sequence;
      forge::asio::blocking::run(runtime, store.async_remove_rendezvous(peer(176), "forge.sequence"));
      forge::asio::blocking::run(runtime, store.async_remove_rendezvous(peer(177), "forge.sequence"));
      forge::asio::blocking::run(runtime, store.async_close());
   }

   auto reopened = peer_store{peer_store::options{.persistence = persistence}};
   forge::asio::blocking::run(runtime, reopened.async_hydrate());
   forge::asio::blocking::run(runtime, reopened.async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.sequence",
                                           .peer = peer(178),
                                           .ttl = std::chrono::hours{1},
                                           .expires_at = expires_at,
                                       }));
   const auto added = reopened.discover_rendezvous("forge.sequence", highest_sequence, 10);
   BOOST_REQUIRE_EQUAL(added.size(), 1U);
   BOOST_TEST(added.front().sequence > highest_sequence);
   BOOST_TEST(persistence->applied_rendezvous_high_watermark == added.front().sequence);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_rejects_exhausted_rendezvous_sequence) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = peer_store::make_memory_persistence();
   auto exhausted = peer_store::mutation_batch{};
   exhausted.rendezvous_sequence_high_watermark = std::numeric_limits<std::uint64_t>::max();
   forge::asio::blocking::run(runtime, persistence->async_apply(std::move(exhausted)));

   auto store = peer_store{peer_store::options{.persistence = persistence}};
   forge::asio::blocking::run(runtime, store.async_hydrate());
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                                .namespace_name = "forge.sequence.exhausted",
                                                .peer = peer(186),
                                                .ttl = std::chrono::hours{1},
                                                .expires_at = std::chrono::system_clock::now() + std::chrono::hours{1},
                                            }))),
       exceptions::sequence_exhausted);
   BOOST_TEST(store.discover_rendezvous("forge.sequence.exhausted", 0, 10).empty());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_memory_persistence_applies_flushes_and_prunes_bounded_state) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .prune_page_limit = 2,
   }};
   const auto now = std::chrono::system_clock::now();
   const auto expired_at = now - std::chrono::seconds{1};
   const auto live_until = now + std::chrono::hours{1};

   store.upsert(peer_store::record{.peer = peer(140), .discovery_expires_at = expired_at});
   store.upsert(peer_store::record{.peer = peer(141), .discovery_expires_at = expired_at});
   store.upsert(peer_store::record{.peer = peer(142), .discovery_expires_at = live_until});
   forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.prune",
                                           .peer = peer(145),
                                           .ttl = std::chrono::hours{1},
                                           .expires_at = expired_at,
                                       }));
   forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.prune",
                                           .peer = peer(146),
                                           .ttl = std::chrono::hours{1},
                                           .expires_at = live_until,
                                       }));
   forge::asio::blocking::run(runtime, store.async_flush());

   BOOST_TEST(persistence->durable_apply_attempts == 2U);

   const auto first = forge::asio::blocking::run(runtime, store.async_prune_expired(now));
   const auto second = forge::asio::blocking::run(runtime, store.async_prune_expired(now));
   const auto third = forge::asio::blocking::run(runtime, store.async_prune_expired(now));
   BOOST_TEST(first.peers.size() == 2U);
   BOOST_TEST(first.may_have_more);
   BOOST_TEST(second.rendezvous_registrations.size() == 1U);
   BOOST_TEST(!second.may_have_more);
   BOOST_TEST(third.peers.empty());
   BOOST_TEST(third.rendezvous_registrations.empty());
   BOOST_TEST(!third.may_have_more);
   BOOST_TEST(std::ranges::all_of(persistence->prune_limits, [](auto limit) { return limit == 2U; }));
   BOOST_REQUIRE_EQUAL(store.snapshot(10).size(), 1U);
   BOOST_REQUIRE_EQUAL(store.discover_rendezvous("forge.prune", 0, 10).size(), 1U);
   forge::asio::blocking::run(runtime, store.async_remove_rendezvous(peer(146), "forge.prune"));
   BOOST_TEST(store.discover_rendezvous("forge.prune", 0, 10).empty());
   BOOST_TEST(persistence->applied_rendezvous_removals == 1U);
   BOOST_TEST(persistence->durable_apply_attempts == 3U);
   forge::asio::blocking::run(runtime, store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_bounds_pending_queue_and_recovers_after_flush) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_peers = 8,
       .max_pending = 2,
   }};

   store.upsert(peer_store::record{.peer = peer(150), .protocol_version = "/forge/queued/1"});
   store.upsert(peer_store::record{.peer = peer(151), .protocol_version = "/forge/queued/1"});
   BOOST_TEST(store.persistence_state().pending_peer_mutations == 2U);
   BOOST_CHECK_THROW(store.upsert(peer_store::record{.peer = peer(152)}), exceptions::backpressure_rejected);
   BOOST_TEST(!store.find(peer(152)).has_value());

   forge::asio::blocking::run(runtime, store.async_flush());
   BOOST_TEST(store.persistence_state().pending_peer_mutations == 0U);
   BOOST_TEST(persistence->applied_peer_upserts == 2U);

   store.upsert(peer_store::record{.peer = peer(152), .protocol_version = "/forge/queued/1"});
   forge::asio::blocking::run(runtime, store.async_flush());
   BOOST_TEST(persistence->applied_peer_upserts == 3U);
   forge::asio::blocking::run(runtime, store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_eviction_supersedes_older_in_flight_upsert_durably) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->block_apply = true;
   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_peers = 1,
       .max_pending = 2,
   }};

   store.upsert(peer_store::record{.peer = peer(181), .protocol_version = "/forge/evicted/1"});
   auto first_flush = boost::asio::co_spawn(runtime.context(), store.async_flush(), boost::asio::use_future);
   BOOST_REQUIRE(persistence->wait_until_apply_blocked());

   store.upsert(peer_store::record{.peer = peer(180), .protocol_version = "/forge/retained/1"});
   BOOST_TEST(!store.find(peer(181)).has_value());
   BOOST_REQUIRE(store.find(peer(180)).has_value());
   BOOST_TEST(store.persistence_state().pending_peer_mutations == 2U);

   persistence->release_apply();
   first_flush.get();
   persistence->block_apply = false;
   forge::asio::blocking::run(runtime, store.async_flush());
   BOOST_TEST(persistence->applied_peer_upserts == 2U);
   BOOST_TEST(persistence->applied_peer_removals == 1U);

   auto reopened = peer_store{peer_store::options{
       .persistence = persistence,
       .max_peers = 1,
       .max_pending = 2,
   }};
   forge::asio::blocking::run(runtime, reopened.async_hydrate());
   BOOST_REQUIRE_EQUAL(reopened.snapshot(10).size(), 1U);
   BOOST_TEST(reopened.find(peer(180)).has_value());
   BOOST_TEST(!reopened.find(peer(181)).has_value());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_bounds_persistence_waiters_before_gate) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->block_apply = true;
   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_persistence_waiters = 1,
   }};
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};

   auto first = boost::asio::co_spawn(runtime.context(),
                                      store.async_upsert_rendezvous(rendezvous::registration{
                                          .namespace_name = "forge.gate",
                                          .peer = peer(182),
                                          .ttl = std::chrono::hours{1},
                                          .expires_at = expires_at,
                                      }),
                                      boost::asio::use_future);
   BOOST_REQUIRE(persistence->wait_until_apply_blocked());
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_flush())), exceptions::backpressure_rejected);

   auto closing = boost::asio::co_spawn(runtime.context(), store.async_close(), boost::asio::use_future);
   for (auto attempt = 0; attempt < 200 && !store.persistence_state().closing; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
   }
   BOOST_REQUIRE(store.persistence_state().closing);
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_close())), exceptions::backpressure_rejected);
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_flush())), exceptions::closed);

   persistence->release_apply();
   first.get();
   persistence->block_apply = false;
   closing.get();
   BOOST_TEST(store.persistence_state().closed);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_canceled_close_releases_bounded_waiter_and_allows_retry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->block_apply = true;
   auto store = peer_store{peer_store::options{
       .persistence = persistence,
       .max_persistence_waiters = 1,
   }};
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};
   auto applying = boost::asio::co_spawn(runtime.context(),
                                         store.async_upsert_rendezvous(rendezvous::registration{
                                             .namespace_name = "forge.close",
                                             .peer = peer(187),
                                             .ttl = std::chrono::hours{1},
                                             .expires_at = expires_at,
                                         }),
                                         boost::asio::use_future);
   BOOST_REQUIRE(persistence->wait_until_apply_blocked());

   auto cancellation = boost::asio::cancellation_signal{};
   auto canceled_close =
       boost::asio::co_spawn(runtime.context(), store.async_close(),
                             boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   for (auto attempt = 0; attempt < 200 && !store.persistence_state().closing; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
   }
   BOOST_REQUIRE(store.persistence_state().closing);
   cancellation.emit(boost::asio::cancellation_type::terminal);
   BOOST_REQUIRE(canceled_close.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   BOOST_CHECK_THROW(canceled_close.get(), exceptions::canceled);

   auto retry = boost::asio::co_spawn(runtime.context(), store.async_close(), boost::asio::use_future);
   persistence->release_apply();
   applying.get();
   persistence->block_apply = false;
   retry.get();
   BOOST_TEST(store.persistence_state().closed);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_prune_preserves_newer_synchronous_peer_mutation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->block_prune = true;
   auto store = peer_store{peer_store::options{.persistence = persistence}};
   const auto expired_at = std::chrono::system_clock::now() - std::chrono::seconds{1};
   const auto id = peer(183);
   store.upsert(peer_store::record{
       .peer = id,
       .protocol_version = "/forge/prune/old",
       .discovery_expires_at = expired_at,
   });

   auto pruning = boost::asio::co_spawn(runtime.context(), store.async_prune_expired(), boost::asio::use_future);
   BOOST_REQUIRE(persistence->wait_until_prune_blocked());
   store.upsert(peer_store::record{
       .peer = id,
       .protocol_version = "/forge/prune/new",
       .discovery_expires_at = expired_at,
   });
   persistence->release_prune();
   const auto result = pruning.get();
   BOOST_REQUIRE_EQUAL(result.peers.size(), 1U);
   BOOST_TEST(result.peers.front().value == id.value);

   const auto current = store.find(id);
   BOOST_REQUIRE(current.has_value());
   BOOST_TEST(current->protocol_version == "/forge/prune/new");
   persistence->block_prune = false;
   forge::asio::blocking::run(runtime, store.async_flush());

   auto reopened = peer_store{peer_store::options{.persistence = persistence}};
   forge::asio::blocking::run(runtime, reopened.async_hydrate());
   const auto durable = reopened.find(id);
   BOOST_REQUIRE(durable.has_value());
   BOOST_TEST(durable->protocol_version == "/forge/prune/new");
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_reports_persistence_failures_and_retries_pending_batch) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto store = peer_store{peer_store::options{.persistence = persistence}};
   store.upsert(peer_store::record{.peer = peer(153), .protocol_version = "/forge/retry/1"});

   persistence->fail_apply = true;
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_flush())), std::runtime_error);
   auto status = store.persistence_state();
   BOOST_TEST(status.pending_peer_mutations == 1U);
   BOOST_TEST(status.failure_count == 1U);
   BOOST_TEST(status.degraded);
   BOOST_TEST(status.last_failure.find("injected peer apply failure") != std::string::npos);

   persistence->fail_apply = false;
   forge::asio::blocking::run(runtime, store.async_flush());
   status = store.persistence_state();
   BOOST_TEST(status.pending_peer_mutations == 0U);
   BOOST_TEST(!status.degraded);
   BOOST_TEST(persistence->applied_peer_upserts == 1U);

   persistence->fail_hydrate = true;
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_hydrate())), std::runtime_error);
   persistence->fail_hydrate = false;
   persistence->fail_prune = true;
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_prune_expired())), std::runtime_error);
   persistence->fail_prune = false;
   store.mark_success(peer(153), path::kind::direct, std::chrono::milliseconds{1});
   persistence->fail_flush = true;
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_flush())), exceptions::durability_uncertain);
   status = store.persistence_state();
   BOOST_TEST(status.pending_peer_mutations == 0U);
   BOOST_TEST(status.degraded);
   persistence->fail_flush = false;
   (void)forge::asio::blocking::run(runtime, store.async_prune_expired());
   BOOST_TEST(store.persistence_state().degraded);
   forge::asio::blocking::run(runtime, store.async_flush());
   BOOST_TEST(!store.persistence_state().degraded);
   BOOST_TEST(store.persistence_state().failure_count == 4U);
   forge::asio::blocking::run(runtime, store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_node_diagnostics_report_peer_persistence_degradation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto options = options_for(peer(232));
   options.peer_state.persistence = persistence;
   auto value = node{runtime, std::move(options)};
   value.peers().upsert(peer_store::record{.peer = peer(233), .protocol_version = "/forge/diagnostics/1"});

   persistence->fail_apply = true;
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, value.peers().async_flush())), std::runtime_error);
   const auto degraded = value.diagnostics().persistence;
   BOOST_TEST(degraded.degraded);
   BOOST_TEST(degraded.failure_count == 1U);
   BOOST_TEST(degraded.pending_peer_mutations == 1U);
   BOOST_TEST(degraded.last_failure.find("injected peer apply failure") != std::string::npos);

   persistence->fail_apply = false;
   forge::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_applies_committed_state_when_durable_acknowledgement_is_uncertain) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   persistence->uncertain_durable_apply = true;
   auto store = peer_store{peer_store::options{.persistence = persistence}};
   const auto registered_peer = peer(188);
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};

   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                                              .namespace_name = "forge.uncertain",
                                                              .peer = registered_peer,
                                                              .ttl = std::chrono::hours{1},
                                                              .expires_at = expires_at,
                                                          }))),
                     exceptions::durability_uncertain);
   const auto status = store.persistence_state();
   BOOST_TEST(status.degraded);
   BOOST_TEST(status.last_failure.find("uncertain durable acknowledgement") != std::string::npos);
   const auto operational = store.discover_rendezvous("forge.uncertain", 0, 1);
   BOOST_REQUIRE_EQUAL(operational.size(), 1U);
   BOOST_TEST(operational.front().peer.value == registered_peer.value);

   auto reopened = peer_store{peer_store::options{.persistence = persistence->delegate}};
   forge::asio::blocking::run(runtime, reopened.async_hydrate());
   const auto durable = reopened.discover_rendezvous("forge.uncertain", 0, 1);
   BOOST_REQUIRE_EQUAL(durable.size(), 1U);
   BOOST_TEST(durable.front().peer.value == registered_peer.value);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_enforces_rendezvous_backpressure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto store = peer_store{peer_store::options{
       .persistence = peer_store::make_memory_persistence(),
       .max_rendezvous = 1,
   }};
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};
   forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.capacity",
                                           .peer = peer(156),
                                           .ttl = std::chrono::hours{1},
                                           .expires_at = expires_at,
                                       }));
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                                              .namespace_name = "forge.capacity",
                                                              .peer = peer(157),
                                                              .ttl = std::chrono::hours{1},
                                                              .expires_at = expires_at,
                                                          }))),
                     exceptions::backpressure_rejected);
   forge::asio::blocking::run(runtime, store.async_upsert_rendezvous(rendezvous::registration{
                                           .namespace_name = "forge.capacity",
                                           .peer = peer(156),
                                           .ttl = std::chrono::hours{1},
                                           .expires_at = expires_at,
                                       }));
   BOOST_REQUIRE_EQUAL(store.discover_rendezvous("forge.capacity", 0, 10).size(), 1U);
   forge::asio::blocking::run(runtime, store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_enforces_rendezvous_capacity_per_peer_before_persistence) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto store = peer_store{peer_store::options{.persistence = persistence}};
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};
   const auto first_peer = peer(227);
   const auto second_peer = peer(228);
   const auto registration = [&](std::string namespace_name, peer_id id) {
      return rendezvous::registration{
          .namespace_name = std::move(namespace_name),
          .peer = std::move(id),
          .ttl = std::chrono::hours{1},
          .expires_at = expires_at,
      };
   };

   forge::asio::blocking::run(runtime, store.async_register_rendezvous(registration("forge.first", first_peer), 1));
   forge::asio::blocking::run(runtime, store.async_register_rendezvous(registration("forge.first", first_peer), 1));
   const auto applied_before_rejection = persistence->applied_rendezvous_upserts;
   BOOST_CHECK_THROW((forge::asio::blocking::run(
                         runtime, store.async_register_rendezvous(registration("forge.second", first_peer), 1))),
                     exceptions::backpressure_rejected);
   BOOST_TEST(persistence->applied_rendezvous_upserts == applied_before_rejection);
   forge::asio::blocking::run(runtime, store.async_register_rendezvous(registration("forge.second", second_peer), 1));
   forge::asio::blocking::run(runtime, store.async_remove_rendezvous(first_peer, "forge.first"));
   forge::asio::blocking::run(runtime, store.async_register_rendezvous(registration("forge.second", first_peer), 1));
   BOOST_REQUIRE_EQUAL(store.discover_rendezvous("forge.second", 0, 10).size(), 2U);
   forge::asio::blocking::run(runtime, store.async_close());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_close_flushes_pending_state_and_is_idempotent) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto store = peer_store{peer_store::options{.persistence = persistence}};
   store.upsert(peer_store::record{.peer = peer(158), .protocol_version = "/forge/close/1"});

   forge::asio::blocking::run(runtime, store.async_close());
   const auto status = store.persistence_state();
   BOOST_TEST(status.pending_peer_mutations == 0U);
   BOOST_TEST(status.closed);
   BOOST_TEST(!status.closing);
   BOOST_TEST(persistence->applied_peer_upserts == 1U);
   BOOST_TEST(persistence->flush_attempts == 1U);
   BOOST_TEST(persistence->close_attempts == 1U);

   forge::asio::blocking::run(runtime, store.async_close());
   BOOST_TEST(persistence->close_attempts == 1U);
   BOOST_CHECK_THROW(store.upsert(peer_store::record{.peer = peer(159)}), exceptions::closed);
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_flush())), exceptions::closed);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_close_failure_is_degraded_and_retryable) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto store = peer_store{peer_store::options{.persistence = persistence}};
   store.upsert(peer_store::record{.peer = peer(160), .protocol_version = "/forge/close-retry/1"});

   persistence->fail_close = true;
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_close())), std::runtime_error);
   auto status = store.persistence_state();
   BOOST_TEST(status.closing);
   BOOST_TEST(!status.closed);
   BOOST_TEST(status.degraded);
   BOOST_TEST(status.last_failure.find("injected peer close failure") != std::string::npos);
   BOOST_CHECK_THROW(store.upsert(peer_store::record{.peer = peer(161)}), exceptions::closed);

   persistence->fail_close = false;
   forge::asio::blocking::run(runtime, store.async_close());
   status = store.persistence_state();
   BOOST_TEST(!status.closing);
   BOOST_TEST(status.closed);
   BOOST_TEST(!status.degraded);
   BOOST_TEST(persistence->flush_attempts == 2U);
   BOOST_TEST(persistence->close_attempts == 2U);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_awaitable_owns_impl_before_first_resume) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   auto pending = std::optional<boost::asio::awaitable<void>>{};
   {
      auto store = peer_store{peer_store::options{.persistence = persistence}};
      pending.emplace(store.async_hydrate());
   }

   forge::asio::blocking::run(runtime, std::move(*pending));
   BOOST_TEST(!persistence->hydration_requests.empty());
}

BOOST_AUTO_TEST_CASE(p2p_dht_awaitables_own_node_impl_before_first_resume) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto identity = make_test_certificate_identity("dht-owned-awaitable");
   auto options = dht_options_for(identity, custom_test_dht_profile());
   options.dht_profiles.push_back(custom_test_value_dht_profile());
   auto advertised = make_dns_tcp_endpoint(4'192, "dht-owned-awaitable.example.com");
   advertised.peer = identity.peer;
   options.advertised_endpoints.push_back(std::move(advertised));
   auto source = node{runtime, std::move(options)};
   forge::asio::blocking::run(runtime, source.async_hydrate_peer_state());

   const auto provider_key = make_dht_key(std::vector<std::uint8_t>{'o', 'w', 'n', 'e', 'd'});
   const auto value_key = make_dht_key(std::vector<std::uint8_t>{'/', 'o', 'w', 'n', 'e', 'd'});
   auto find_peer = source.async_find_peer(content_swarm_test_dht, peer(219));
   auto provide = source.async_provide(content_swarm_test_dht, provider_key);
   auto find_providers = source.async_find_providers(content_swarm_test_dht, provider_key);
   auto put_value =
       source.async_put_value(content_swarm_value_test_dht, dht::record{.key_value = value_key, .value = {'v'}});
   auto get_value = source.async_get_value(content_swarm_value_test_dht, value_key);
   auto owner = std::move(source);

   const auto peer_result = forge::asio::blocking::run(runtime, std::move(find_peer));
   BOOST_TEST(!peer_result.complete);
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, std::move(provide))), exceptions::peer_not_found);
   BOOST_TEST(forge::asio::blocking::run(runtime, std::move(find_providers)).empty());
   const auto put_result = forge::asio::blocking::run(runtime, std::move(put_value));
   BOOST_TEST(!put_result.quorum_reached);
   const auto get_result = forge::asio::blocking::run(runtime, std::move(get_value));
   BOOST_REQUIRE(get_result.selected.has_value());
   BOOST_TEST(get_result.selected->value == std::vector<std::uint8_t>{'v'});

   forge::asio::blocking::run(runtime, owner.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_provider_removal_failure_allows_stop_retry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("dht-stop-retry-server");
   const auto provider_identity = make_test_certificate_identity("dht-stop-retry-provider");
   auto server = node{runtime, dht_options_for(server_identity, custom_test_dht_profile(dht::mode::server))};
   auto persistence = std::make_shared<tracking_dht_record_store_persistence>();
   auto provider_options = dht_options_for(provider_identity, custom_test_dht_profile());
   provider_options.dht_record_persistence.emplace(content_swarm_test_dht, persistence);
   auto provider_endpoint = make_dns_tcp_endpoint(4'193, "dht-stop-retry.example.com");
   provider_endpoint.peer = provider_identity.peer;
   provider_options.advertised_endpoints.push_back(provider_endpoint);
   auto provider = node{runtime, std::move(provider_options)};
   const auto server_endpoint = listen(server, runtime);
   static_cast<void>(listen(provider, runtime));
   verify_dht_server(runtime, provider, server, server_endpoint, content_swarm_test_dht);
   forge::asio::blocking::run(runtime, provider.async_hydrate_peer_state());

   const auto key = make_dht_key(std::vector<std::uint8_t>{'s', 't', 'o', 'p', '-', 'r', 'e', 't', 'r', 'y'});
   auto registration = forge::asio::blocking::run(runtime, provider.async_provide(content_swarm_test_dht, key));
   BOOST_REQUIRE(registration.active());
   persistence->reject_next_provider_removal.store(true, std::memory_order_relaxed);

   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, provider.async_stop())), std::runtime_error);
   BOOST_TEST(persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 1U);
   BOOST_TEST(persistence->close_attempts.load(std::memory_order_relaxed) == 0U);
   BOOST_TEST(!registration.active());

   forge::asio::blocking::run(runtime, provider.async_stop());
   BOOST_TEST(persistence->provider_remove_attempts.load(std::memory_order_relaxed) == 2U);
   BOOST_TEST(persistence->close_attempts.load(std::memory_order_relaxed) == 1U);
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_production_options_reject_missing_mtls_identity) {
   try {
      validate(node::options{});
      BOOST_FAIL("expected missing mTLS identity rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }
}

BOOST_AUTO_TEST_CASE(p2p_production_options_require_peer_state_persistence) {
   try {
      validate(node::options{
          .certificate_pem = std::string{test_certificate()},
          .private_key_pem = std::string{test_private_key()},
      });
      BOOST_FAIL("expected missing persistent peer store rejection");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(forge::net::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }
}

BOOST_AUTO_TEST_CASE(p2p_node_options_require_bounded_identify_decode_memory) {
   auto options = options_for(peer(249));
   options.limits.resources.max_queued_bytes = options.identify.max_message_size;

   BOOST_CHECK_THROW(validate(options), exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(p2p_node_options_reject_zero_global_dial_limit) {
   auto options = options_for(peer(250));
   options.limits.resources.max_dial_attempts = 0;

   BOOST_CHECK_THROW(validate(options), exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(p2p_production_options_use_peer_state_persistence) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<tracking_peer_store_persistence>();
   const auto identity = make_test_certificate_identity("production-peer-state");
   auto value = node{runtime, node::options{
                                  .certificate_pem = identity.certificate_pem,
                                  .private_key_pem = identity.private_key_pem,
                                  .explicit_peer_id = identity.peer,
                                  .peer_state = peer_store::options{.persistence = persistence},
                              }};
   value.peers().upsert(peer_store::record{
       .peer = peer(84),
       .protocol_version = "/forge/node-memory/1",
       .agent_version = "forge-node-memory/1",
       .protocols = std::vector<protocol_id>{builtins::identify_push},
   });
   forge::asio::blocking::run(runtime, value.async_stop());

   BOOST_TEST(persistence->applied_peer_upserts == 1U);
   BOOST_TEST(persistence->flush_attempts == 1U);
   BOOST_TEST(persistence->close_attempts == 1U);
}

} // namespace forge::net::p2p

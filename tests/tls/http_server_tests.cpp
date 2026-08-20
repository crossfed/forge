#include <boost/test/unit_test.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

import forge.asio.runtime;
import forge.asio.blocking;
import forge.net.http.body;
import forge.net.http.route_context;
import forge.net.http.router;
import forge.net.http.server;
import forge.net.http.stream;
import forge.net.http.types;
import forge.net.http.base_url;
import forge.net.tls.context;
import forge.net.websocket.client;
import forge.net.websocket.connection;

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
using tcp = asio::ip::tcp;

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

struct evp_pkey_context_deleter {
   void operator()(EVP_PKEY_CTX* value) const noexcept {
      EVP_PKEY_CTX_free(value);
   }
};

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;
using evp_pkey_context_ptr = std::unique_ptr<EVP_PKEY_CTX, evp_pkey_context_deleter>;
using x509_ptr = std::unique_ptr<X509, x509_deleter>;
using bio_ptr = std::unique_ptr<BIO, bio_deleter>;

struct identity_material {
   std::string certificate;
   std::string private_key;
};

struct mutual_tls_material {
   identity_material ca;
   identity_material server;
   identity_material client;
};

[[nodiscard]] evp_pkey_ptr make_key() {
   auto context = evp_pkey_context_ptr{EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
   if (!context || EVP_PKEY_keygen_init(context.get()) != 1 ||
       EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) != 1) {
      throw std::runtime_error{"could not create TLS test key"};
   }
   auto* key = static_cast<EVP_PKEY*>(nullptr);
   if (EVP_PKEY_keygen(context.get(), &key) != 1) {
      throw std::runtime_error{"could not generate TLS test key"};
   }
   return evp_pkey_ptr{key};
}

void add_extension(X509* certificate, X509* issuer, int nid, std::string_view value) {
   auto context = X509V3_CTX{};
   X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
   auto* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, std::string{value}.c_str());
   if (extension == nullptr || X509_add_ext(certificate, extension, -1) != 1) {
      X509_EXTENSION_free(extension);
      throw std::runtime_error{"could not add TLS test certificate extension"};
   }
   X509_EXTENSION_free(extension);
}

[[nodiscard]] x509_ptr make_certificate(EVP_PKEY* subject_key, std::string_view common_name, std::uint32_t serial,
                                        X509* issuer, EVP_PKEY* issuer_key, bool certificate_authority,
                                        std::string_view extended_key_usage = {}) {
   auto certificate = x509_ptr{X509_new()};
   if (!certificate || X509_set_version(certificate.get(), 2) != 1 ||
       ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), static_cast<long>(serial)) != 1 ||
       !X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0) ||
       !X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 60 * 60) ||
       X509_set_pubkey(certificate.get(), subject_key) != 1) {
      throw std::runtime_error{"could not create TLS test certificate"};
   }
   auto* subject = X509_get_subject_name(certificate.get());
   if (!subject || X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                              reinterpret_cast<const unsigned char*>(common_name.data()),
                                              static_cast<int>(common_name.size()), -1, 0) != 1) {
      throw std::runtime_error{"could not sign TLS test certificate"};
   }
   if (issuer == nullptr) {
      issuer = certificate.get();
      issuer_key = subject_key;
   }
   if (X509_set_issuer_name(certificate.get(), X509_get_subject_name(issuer)) != 1) {
      throw std::runtime_error{"could not set TLS test certificate issuer"};
   }
   add_extension(certificate.get(), issuer, NID_basic_constraints,
                 certificate_authority ? "critical,CA:TRUE" : "CA:FALSE");
   add_extension(certificate.get(), issuer, NID_key_usage,
                 certificate_authority ? "critical,keyCertSign,cRLSign" : "digitalSignature,keyEncipherment");
   if (!extended_key_usage.empty()) {
      add_extension(certificate.get(), issuer, NID_ext_key_usage, extended_key_usage);
   }
   if (X509_sign(certificate.get(), issuer_key, EVP_sha256()) == 0) {
      throw std::runtime_error{"could not sign TLS test certificate"};
   }
   return certificate;
}

[[nodiscard]] std::string read_bio(BIO* value) {
   const auto* bytes = static_cast<char*>(nullptr);
   const auto size = BIO_get_mem_data(value, &bytes);
   if (size <= 0) {
      throw std::runtime_error{"could not encode TLS test identity"};
   }
   return {bytes, static_cast<std::size_t>(size)};
}

[[nodiscard]] identity_material encode_identity(X509* certificate, EVP_PKEY* key) {
   auto certificate_output = bio_ptr{BIO_new(BIO_s_mem())};
   auto private_key_output = bio_ptr{BIO_new(BIO_s_mem())};
   if (!certificate_output || !private_key_output || PEM_write_bio_X509(certificate_output.get(), certificate) != 1 ||
       PEM_write_bio_PrivateKey(private_key_output.get(), key, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      throw std::runtime_error{"could not write TLS test identity"};
   }
   return {.certificate = read_bio(certificate_output.get()), .private_key = read_bio(private_key_output.get())};
}

[[nodiscard]] identity_material make_identity() {
   const auto key = make_key();
   const auto certificate = make_certificate(key.get(), "forge http tls test", 1, nullptr, nullptr, true);
   return encode_identity(certificate.get(), key.get());
}

[[nodiscard]] mutual_tls_material make_mutual_tls_material() {
   const auto ca_key = make_key();
   const auto server_key = make_key();
   const auto client_key = make_key();
   const auto ca_certificate = make_certificate(ca_key.get(), "forge http tls test CA", 1, nullptr, nullptr, true);
   const auto server_certificate = make_certificate(server_key.get(), "forge http tls server", 2, ca_certificate.get(),
                                                    ca_key.get(), false, "serverAuth");
   const auto client_certificate = make_certificate(client_key.get(), "forge http tls client", 3, ca_certificate.get(),
                                                    ca_key.get(), false, "clientAuth");
   auto ca = encode_identity(ca_certificate.get(), ca_key.get());
   auto client = encode_identity(client_certificate.get(), client_key.get());
   client.certificate += ca.certificate;
   return {.ca = std::move(ca),
           .server = encode_identity(server_certificate.get(), server_key.get()),
           .client = std::move(client)};
}

[[nodiscard]] forge::net::tls::context_options server_options(const identity_material& identity, bool mutual = false,
                                                              std::string client_ca = {}) {
   return forge::net::tls::context_options{
       .role = forge::net::tls::endpoint_role::server,
       .protocols = forge::net::tls::protocol_policy::tls13_only,
       .verification = mutual ? forge::net::tls::peer_verification::require_peer_certificate
                              : forge::net::tls::peer_verification::none,
       .certificate_chain_pem = identity.certificate,
       .private_key_pem = identity.private_key,
       .trust_anchors_pem =
           mutual ? std::vector<std::string>{client_ca.empty() ? identity.certificate : std::move(client_ca)}
                  : std::vector<std::string>{},
       .alpn_protocols = {"http/1.1"},
       .use_default_verify_paths = false,
   };
}

[[nodiscard]] std::uint16_t wait_for_port(const forge::net::http::server& server) {
   for (auto attempt = 0; attempt != 100; ++attempt) {
      if (const auto port = server.port(); port != 0) {
         return port;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
   }
   throw std::runtime_error{"TLS HTTP server did not bind a port"};
}

struct tls_exchange {
   std::string protocol;
   beast_http::response<beast_http::string_body> response;
};

class tls_http_connection {
 public:
   explicit tls_http_connection(std::uint16_t port, const identity_material* client_identity = nullptr)
       : context_(asio::ssl::context::tls_client) {
      context_.set_verify_mode(asio::ssl::verify_none);
      if (client_identity != nullptr) {
         context_.use_certificate_chain(asio::buffer(client_identity->certificate));
         context_.use_private_key(asio::buffer(client_identity->private_key), asio::ssl::context::pem);
      }
      stream_.emplace(beast::tcp_stream{io_}, context_);
      beast::get_lowest_layer(stream()).expires_after(std::chrono::seconds{2});
      beast::get_lowest_layer(stream()).connect({asio::ip::make_address("127.0.0.1"), port});
      stream().handshake(asio::ssl::stream_base::client);
   }

   [[nodiscard]] tls_exchange get(bool keep_alive = false) {
      auto request = beast_http::request<beast_http::empty_body>{beast_http::verb::get, "/ready", 11};
      request.set(beast_http::field::host, "127.0.0.1");
      request.keep_alive(keep_alive);
      beast_http::write(stream(), request);
      auto buffer = beast::flat_buffer{};
      auto response = beast_http::response<beast_http::string_body>{};
      beast_http::read(stream(), buffer, response);
      return {.protocol = SSL_get_version(stream().native_handle()), .response = std::move(response)};
   }

   [[nodiscard]] bool has_client_identity() noexcept {
      return SSL_get_certificate(stream().native_handle()) != nullptr &&
             SSL_get_privatekey(stream().native_handle()) != nullptr;
   }

   [[nodiscard]] int requested_client_authority_count() noexcept {
      const auto* authorities = SSL_get_client_CA_list(stream().native_handle());
      return authorities == nullptr ? 0 : sk_X509_NAME_num(authorities);
   }

   void get_stream_header_then_abort() {
      auto request = beast_http::request<beast_http::empty_body>{beast_http::verb::get, "/stream", 11};
      request.set(beast_http::field::host, "127.0.0.1");
      beast_http::write(stream(), request);

      auto buffer = beast::flat_buffer{};
      auto parser = beast_http::response_parser<beast_http::empty_body>{};
      beast_http::read_header(stream(), buffer, parser);

      auto ignored = boost::system::error_code{};
      beast::get_lowest_layer(stream()).socket().set_option(asio::socket_base::linger{true, 0}, ignored);
      beast::get_lowest_layer(stream()).socket().close(ignored);
   }

   [[nodiscard]] boost::system::error_code get_then_shutdown() {
      auto request = beast_http::request<beast_http::empty_body>{beast_http::verb::get, "/ready", 11};
      request.set(beast_http::field::host, "127.0.0.1");
      request.keep_alive(false);
      beast_http::write(stream(), request);
      auto buffer = beast::flat_buffer{};
      auto response = beast_http::response<beast_http::string_body>{};
      beast_http::read(stream(), buffer, response);
      auto error = boost::system::error_code{};
      stream().shutdown(error);
      return error;
   }

   [[nodiscard]] boost::system::error_code get_keep_alive_then_shutdown() {
      auto request = beast_http::request<beast_http::empty_body>{beast_http::verb::get, "/ready", 11};
      request.set(beast_http::field::host, "127.0.0.1");
      request.keep_alive(true);
      beast_http::write(stream(), request);
      auto buffer = beast::flat_buffer{};
      auto response = beast_http::response<beast_http::string_body>{};
      beast_http::read(stream(), buffer, response);
      auto error = boost::system::error_code{};
      stream().shutdown(error);
      return error;
   }

   [[nodiscard]] boost::system::error_code shutdown_before_request() {
      auto error = boost::system::error_code{};
      stream().shutdown(error);
      return error;
   }

 private:
   [[nodiscard]] beast::ssl_stream<beast::tcp_stream>& stream() noexcept {
      return *stream_;
   }

   asio::io_context io_;
   asio::ssl::context context_;
   std::optional<beast::ssl_stream<beast::tcp_stream>> stream_;
};

[[nodiscard]] tls_exchange https_get(std::uint16_t port, const identity_material* client_identity = nullptr,
                                     bool keep_alive = false) {
   auto connection = tls_http_connection{port, client_identity};
   return connection.get(keep_alive);
}

struct tls_handshake_attempt {
   bool handshake_completed = false;
   bool request_completed = false;
   bool client_identity_loaded = false;
   int requested_client_authority_count = 0;
   std::string failure;
};

[[nodiscard]] tls_handshake_attempt tls_handshake_succeeds(std::uint16_t port,
                                                           const identity_material* client_identity) {
   auto attempt = tls_handshake_attempt{};
   try {
      auto connection = tls_http_connection{port, client_identity};
      attempt.handshake_completed = true;
      attempt.client_identity_loaded = connection.has_client_identity();
      attempt.requested_client_authority_count = connection.requested_client_authority_count();
      static_cast<void>(connection.get());
      attempt.request_completed = true;
   } catch (const boost::system::system_error& error) {
      attempt.failure = error.code().category().name();
      attempt.failure += ':';
      attempt.failure += std::to_string(error.code().value());
      attempt.failure += " (";
      attempt.failure += error.code().message();
      attempt.failure += "): ";
      attempt.failure += error.what();
   } catch (const std::exception& error) {
      attempt.failure = error.what();
   }
   return attempt;
}

[[nodiscard]] forge::net::http::router ready_router() {
   auto router = forge::net::http::router{};
   router.get("/ready",
              [](forge::net::http::route_context& context) -> boost::asio::awaitable<forge::net::http::response> {
                 co_return forge::net::http::make_text_response(context.request, forge::net::http::status::ok, "ready");
              });
   return router;
}

} // namespace

BOOST_AUTO_TEST_CASE(http_server_accepts_tls_1_3_before_parsing_http) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   const auto result = https_get(wait_for_port(server));
   BOOST_TEST(result.protocol == "TLSv1.3");
   BOOST_TEST(result.response.result() == beast_http::status::ok);
   BOOST_TEST(result.response.body() == "ready");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_mutual_tls_verifies_client_chain) {
   const auto material = make_mutual_tls_material();
   auto provider = std::make_shared<forge::net::tls::context_provider>(
       server_options(material.server, true, material.ca.certificate));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   const auto port = wait_for_port(server);
   const auto trusted_client = tls_handshake_succeeds(port, &material.client);
   BOOST_TEST_CONTEXT("trusted client TLS attempt: handshake="
                      << trusted_client.handshake_completed << ", request=" << trusted_client.request_completed
                      << ", identity=" << trusted_client.client_identity_loaded << ", requested CAs="
                      << trusted_client.requested_client_authority_count << ", failure=" << trusted_client.failure) {
      BOOST_CHECK(trusted_client.client_identity_loaded);
      BOOST_CHECK_EQUAL(trusted_client.requested_client_authority_count, 1);
      BOOST_CHECK(trusted_client.request_completed);
   }

   const auto no_client = tls_handshake_succeeds(port, nullptr);
   BOOST_TEST_CONTEXT("no-client TLS attempt: handshake="
                      << no_client.handshake_completed << ", request=" << no_client.request_completed
                      << ", identity=" << no_client.client_identity_loaded << ", requested CAs="
                      << no_client.requested_client_authority_count << ", failure=" << no_client.failure) {
      BOOST_CHECK(!no_client.client_identity_loaded);
      BOOST_CHECK_EQUAL(no_client.requested_client_authority_count, 1);
      BOOST_CHECK(!no_client.request_completed);
      BOOST_CHECK(no_client.failure.find("certificate required") != std::string::npos);
   }
   const auto untrusted = make_mutual_tls_material();
   const auto untrusted_client = tls_handshake_succeeds(port, &untrusted.client);
   BOOST_TEST_CONTEXT("untrusted client TLS attempt: handshake="
                      << untrusted_client.handshake_completed << ", request=" << untrusted_client.request_completed
                      << ", identity=" << untrusted_client.client_identity_loaded
                      << ", requested CAs=" << untrusted_client.requested_client_authority_count
                      << ", failure=" << untrusted_client.failure) {
      BOOST_CHECK(untrusted_client.client_identity_loaded);
      BOOST_CHECK_EQUAL(untrusted_client.requested_client_authority_count, 1);
      BOOST_CHECK(!untrusted_client.request_completed);
   }

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_mutual_tls_loads_every_certificate_from_trust_anchor_bundle) {
   const auto server_identity = make_identity();
   const auto first_ca_key = make_key();
   const auto second_ca_key = make_key();
   const auto client_key = make_key();
   const auto first_ca_certificate =
       make_certificate(first_ca_key.get(), "forge http first client CA", 1, nullptr, nullptr, true);
   const auto second_ca_certificate =
       make_certificate(second_ca_key.get(), "forge http second client CA", 2, nullptr, nullptr, true);
   const auto client_certificate =
       make_certificate(client_key.get(), "forge http second CA client", 3, second_ca_certificate.get(),
                        second_ca_key.get(), false, "clientAuth");
   const auto first_ca = encode_identity(first_ca_certificate.get(), first_ca_key.get());
   const auto second_ca = encode_identity(second_ca_certificate.get(), second_ca_key.get());
   auto client = encode_identity(client_certificate.get(), client_key.get());
   client.certificate += second_ca.certificate;

   auto provider = std::make_shared<forge::net::tls::context_provider>(
       server_options(server_identity, true, first_ca.certificate + second_ca.certificate));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   const auto trusted_second_ca_client = tls_handshake_succeeds(wait_for_port(server), &client);
   BOOST_TEST_CONTEXT("second-CA client TLS attempt: request="
                      << trusted_second_ca_client.request_completed
                      << ", identity=" << trusted_second_ca_client.client_identity_loaded
                      << ", requested CAs=" << trusted_second_ca_client.requested_client_authority_count
                      << ", failure=" << trusted_second_ca_client.failure) {
      BOOST_CHECK(trusted_second_ca_client.client_identity_loaded);
      BOOST_CHECK_EQUAL(trusted_second_ca_client.requested_client_authority_count, 2);
      BOOST_CHECK(trusted_second_ca_client.request_completed);
   }

   server.stop();
}

BOOST_AUTO_TEST_CASE(tls_context_rejects_malformed_trailing_trust_anchor_bundle) {
   const auto identity = make_identity();
   BOOST_CHECK_THROW(static_cast<void>(std::make_shared<forge::net::tls::context_provider>(
                         server_options(identity, true, identity.certificate + "malformed trailing PEM"))),
                     forge::net::tls::exceptions::trust_anchors_invalid);
}

BOOST_AUTO_TEST_CASE(http_server_tls_has_no_plaintext_fallback_and_bounds_pending_handshakes) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::milliseconds{75},
        .max_pending_tls_handshakes = 1},
       ready_router(),
   };
   server.start();

   const auto port = wait_for_port(server);
   auto io = asio::io_context{};
   auto first = tcp::socket{io};
   first.connect({asio::ip::make_address("127.0.0.1"), port});
   auto second = tcp::socket{io};
   second.connect({asio::ip::make_address("127.0.0.1"), port});
   auto write_error = boost::system::error_code{};
   asio::write(first, asio::buffer(std::string_view{"GET /ready HTTP/1.1\r\nHost: localhost\r\n\r\n"}), write_error);
   std::this_thread::sleep_for(std::chrono::milliseconds{125});

   second.non_blocking(true);
   auto one_byte = std::array<char, 1>{};
   auto error = boost::system::error_code{};
   static_cast<void>(second.read_some(asio::buffer(one_byte), error));
   BOOST_CHECK(error == asio::error::eof || error == asio::error::connection_reset ||
               error == asio::error::operation_aborted);

   first.non_blocking(true);
   error = {};
   static_cast<void>(first.read_some(asio::buffer(one_byte), error));
   BOOST_CHECK(write_error == boost::system::error_code{} || write_error == asio::error::broken_pipe ||
               write_error == asio::error::connection_reset);
   BOOST_CHECK(error == asio::error::eof || error == asio::error::connection_reset ||
               error == asio::error::operation_aborted);
   BOOST_CHECK(tls_handshake_succeeds(port, nullptr).request_completed);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_tls_rotation_keeps_established_http_sessions_usable) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   const auto original = provider->snapshot();
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   auto established = tls_http_connection{wait_for_port(server)};
   const auto first = established.get(true);
   const auto replacement_identity = make_identity();
   provider->replace(server_options(replacement_identity));
   const auto retained = established.get(true);
   const auto second = https_get(wait_for_port(server));

   BOOST_REQUIRE(original);
   BOOST_CHECK_NE(original.get(), provider->snapshot().get());
   BOOST_TEST(first.response.result() == beast_http::status::ok);
   BOOST_TEST(retained.response.result() == beast_http::status::ok);
   BOOST_TEST(second.response.result() == beast_http::status::ok);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_shutdown_cancels_pending_tls_handshakes_without_waiting_for_close_notify) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{30},
        .max_pending_tls_handshakes = 1},
       ready_router(),
   };
   server.start();

   auto io = asio::io_context{};
   auto socket = tcp::socket{io};
   socket.connect({asio::ip::make_address("127.0.0.1"), wait_for_port(server)});
   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_server_tls_normal_close_sends_close_notify) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   auto connection = tls_http_connection{wait_for_port(server)};
   const auto close_error = connection.get_then_shutdown();
   BOOST_CHECK(close_error != asio::ssl::error::stream_truncated);
   BOOST_CHECK(!close_error || close_error == asio::error::eof);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_tls_reciprocates_client_close_notify_while_keep_alive_idle) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   auto connection = tls_http_connection{wait_for_port(server)};
   const auto close_error = connection.get_keep_alive_then_shutdown();
   BOOST_CHECK(close_error != asio::ssl::error::stream_truncated);
   BOOST_CHECK(!close_error || close_error == asio::error::eof);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_tls_reciprocates_client_close_notify_before_first_request) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       ready_router(),
   };
   server.start();

   auto connection = tls_http_connection{wait_for_port(server)};
   const auto close_error = connection.shutdown_before_request();
   BOOST_CHECK(close_error != asio::ssl::error::stream_truncated);
   BOOST_CHECK(!close_error || close_error == asio::error::eof);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_tls_websocket_handoff_retains_the_connection_snapshot) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = ready_router();
   router.websocket("/ws", [](forge::net::websocket::connection::ptr connection) {
      connection->on_message(
          [](forge::net::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
             co_await connection.send(std::move(message));
          });
   });
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       std::move(router),
   };
   server.start();

   auto client = forge::net::websocket::client{
       runtime, forge::net::http::parse_base_url("wss://127.0.0.1:" + std::to_string(wait_for_port(server)))};
   auto connection = client.connect("/ws", {.verify_peer = false});
   provider->replace(server_options(make_identity()));

   auto received_mutex = std::mutex{};
   auto received_ready = std::condition_variable{};
   auto received = std::string{};
   auto ready = false;
   connection->on_message([&](forge::net::websocket::connection&, std::string message) -> boost::asio::awaitable<void> {
      {
         const auto lock = std::scoped_lock{received_mutex};
         received = std::move(message);
         ready = true;
      }
      received_ready.notify_all();
      co_return;
   });
   forge::asio::blocking::run(runtime, connection->send("retained"));

   {
      auto lock = std::unique_lock{received_mutex};
      BOOST_REQUIRE(received_ready.wait_for(lock, std::chrono::seconds{2}, [&] { return ready; }));
   }
   BOOST_TEST(received == "retained");
   forge::asio::blocking::run(runtime, connection->close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(http_server_tls_disconnect_cancels_streaming_body_owner) {
   const auto identity = make_identity();
   auto provider = std::make_shared<forge::net::tls::context_provider>(server_options(identity));
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto body_started = std::make_shared<std::atomic_bool>(false);
   auto body_canceled = std::make_shared<std::atomic_bool>(false);
   auto router = forge::net::http::router{};
   router.get_stream(
       "/stream",
       [body_started, body_canceled](forge::net::http::stream_request& request_value)
           -> boost::asio::awaitable<forge::net::http::stream_response> {
          auto complete = std::make_shared<std::atomic_bool>(false);
          co_return forge::net::http::stream_response{
              .head = forge::net::http::response{forge::net::http::status::ok, request_value.context.request.version()},
              .body = [body_started, body_canceled,
                       complete]() -> boost::asio::awaitable<std::optional<forge::net::http::body_chunk>> {
                 if (complete->exchange(true)) {
                    co_return std::nullopt;
                 }
                 body_started->store(true);
                 auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
                 timer.expires_after(std::chrono::seconds{2});
                 auto error = boost::system::error_code{};
                 co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
                 if (error == boost::asio::error::operation_aborted) {
                    body_canceled->store(true);
                 }
                 co_return std::nullopt;
              },
          };
       });
   auto server = forge::net::http::server{
       runtime,
       {.tls_context_provider = provider,
        .handshake_timeout = std::chrono::seconds{1},
        .max_pending_tls_handshakes = 4},
       std::move(router),
   };
   server.start();

   auto client = tls_http_connection{wait_for_port(server)};
   client.get_stream_header_then_abort();
   for (auto attempt = 0; attempt != 100 && !body_canceled->load(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
   }
   BOOST_TEST(body_started->load());
   BOOST_TEST(body_canceled->load());

   server.stop();
}

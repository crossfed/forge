module;

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

export module forge.net.stcp.options;

export import forge.net.tls.options;

export namespace forge::net::stcp {

using peer_certificate = tls::peer_certificate;
using certificate_chain = tls::certificate_chain;
using peer_verifier = tls::peer_verifier;
using sni_policy = tls::sni_policy;

struct security_options {
   bool verify_peer = true;
   bool require_peer_certificate = false;
   std::string trusted_ca_pem;
   std::optional<std::string> expected_sha256_fingerprint;
   peer_verifier verifier;
};

struct client_options {
   security_options security;
   std::string certificate_pem;
   std::string private_key_pem;
   std::string server_name;
   sni_policy sni = sni_policy::endpoint_host;
   std::vector<std::string> alpn_protocols;
   std::size_t read_chunk_size = 64 * 1024;
   bool tls13_only = true;
};

struct server_options {
   security_options security{.verify_peer = false};
   std::string certificate_pem;
   std::string private_key_pem;
   std::vector<std::string> alpn_protocols;
   std::size_t read_chunk_size = 64 * 1024;
   bool tls13_only = true;
};

} // namespace forge::net::stcp

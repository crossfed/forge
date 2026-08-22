module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module forge.net.tls.options;

export namespace forge::net::tls {

inline constexpr std::size_t max_certificate_chain_pem_bytes = 1024U * 1024U;
inline constexpr std::size_t max_private_key_pem_bytes = 256U * 1024U;
inline constexpr std::size_t max_trust_anchors = 32U;
inline constexpr std::size_t max_trust_anchor_pem_bytes = 1024U * 1024U;
inline constexpr std::size_t max_trust_anchor_pem_total_bytes = 4U * 1024U * 1024U;
inline constexpr std::size_t max_alpn_protocols = 32U;

enum class endpoint_role : std::uint8_t {
   client,
   server,
};

enum class protocol_policy : std::uint8_t {
   tls13_only,
   system_default,
};

enum class peer_verification : std::uint8_t {
   none,
   verify_peer,
   require_peer_certificate,
   require_peer_certificate_for_application_verification,
};

enum class sni_policy : std::uint8_t {
   endpoint_host,
   explicit_name,
   disabled,
};

struct peer_certificate {
   std::vector<std::uint8_t> der;
   std::string sha256_fingerprint;
};

struct certificate_chain {
   std::vector<peer_certificate> certificates;
};

using peer_verifier = std::function<bool(const certificate_chain&)>;

struct peer_validation {
   std::string expected_host;
   std::optional<std::string> expected_sha256_fingerprint;
   peer_verifier verifier;
};

struct client_stream_options {
   sni_policy sni = sni_policy::endpoint_host;
   std::string endpoint_host;
   std::string server_name;
};

struct context_options {
   endpoint_role role = endpoint_role::client;
   protocol_policy protocols = protocol_policy::tls13_only;
   peer_verification verification = peer_verification::verify_peer;
   std::string certificate_chain_pem;
   std::string private_key_pem;
   std::vector<std::string> trust_anchors_pem;
   std::vector<std::string> alpn_protocols;
   bool use_default_verify_paths = true;
};

} // namespace forge::net::tls

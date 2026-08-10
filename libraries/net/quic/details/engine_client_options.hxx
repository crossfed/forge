#pragma once

extern "C++" {
namespace forge::net::quic::detail {

struct engine_client_options {
   std::string alpn = "forge-p2p/1";
   std::chrono::milliseconds connect_timeout{10'000};
   std::chrono::milliseconds handshake_timeout{10'000};
   std::chrono::milliseconds idle_timeout{30'000};
   engine_transport_limits limits{};
   engine_security_options security{};
   std::string certificate_pem;
   forge::crypto::core::secret_string private_key_pem;
   std::function<bool(std::string_view)> test_failpoint;
};

} // namespace forge::net::quic::detail
}

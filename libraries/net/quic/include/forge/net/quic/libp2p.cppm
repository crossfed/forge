module;
#include <string_view>

export module forge.net.quic.libp2p;

import forge.net.quic.options;

export namespace forge::net::quic::libp2p {

inline constexpr auto alpn = std::string_view{"libp2p"};

[[nodiscard]] client_options client_profile(client_options options = {});
[[nodiscard]] server_options server_profile(server_options options = {});
[[nodiscard]] bool is_profile_alpn(std::string_view value) noexcept;

} // namespace forge::net::quic::libp2p

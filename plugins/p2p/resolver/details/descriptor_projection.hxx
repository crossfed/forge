#pragma once

namespace forge::plugins::p2p::resolver {

inline constexpr auto resolver_api_id = "forge.plugins.p2p.resolver.protocol";

[[nodiscard]] std::string api_key(const forge::api::core::api_id& id, std::uint16_t major);
[[nodiscard]] entry project_descriptor(const forge::api::core::descriptor& descriptor,
                                       const forge::net::p2p::protocol_id& protocol,
                                       const forge::api::transport::options& options);
void validate_entry(const entry& value, const config& limits, std::string_view source);
void validate_response(const std::vector<entry>& entries, const config& limits);
void validate_descriptor_compatible(const forge::api::core::descriptor& descriptor, const entry& remote);
[[nodiscard]] std::optional<entry>
select_compatible(const std::vector<entry>& entries, const forge::api::core::api_ref& requested);

} // namespace forge::plugins::p2p::resolver

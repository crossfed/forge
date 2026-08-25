module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module package.chain_api_component.test_support;

export import forge.chain.protocol.audit;
export import forge.chain.protocol.info;

export namespace package_chain_api_component {

inline constexpr auto chain_api_max_request_size = std::uint32_t{64U * 1024U};

void require(bool condition, std::string_view message);
[[nodiscard]] forge::chain::protocol::digest hash(std::string_view value);
[[nodiscard]] forge::chain::protocol::service_limits package_limits();
[[nodiscard]] forge::chain::protocol::info_response make_audited_info_response();
void require_audit_semantics(const forge::chain::protocol::audited_response& response,
                             std::size_t state_proofs = 2U);

} // namespace package_chain_api_component

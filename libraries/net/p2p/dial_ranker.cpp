module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.net.p2p.dialing;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.multiformats.multiaddr;

#include "details/dial_ranker.hxx"
#include "details/host_addresses.hxx"

namespace forge::net::p2p::detail {
namespace {

constexpr auto maximum_public_delay = std::chrono::milliseconds{250};
constexpr auto maximum_private_delay = std::chrono::milliseconds{30};

[[nodiscard]] bool is_private_scope(host_addresses::endpoint_scope value) noexcept {
   return value == host_addresses::endpoint_scope::private_address ||
          value == host_addresses::endpoint_scope::loopback || value == host_addresses::endpoint_scope::link_local;
}

[[nodiscard]] bool is_supported_direct(const endpoint& value) noexcept {
   return value.is_direct_quic() || value.is_direct_tcp();
}

[[nodiscard]] bool is_ipv6(const endpoint& value) noexcept {
   return value.transport.host_type == endpoint::host_kind::ip6;
}

[[nodiscard]] bool is_ipv4(const endpoint& value) noexcept {
   return value.transport.host_type == endpoint::host_kind::ip4;
}

[[nodiscard]] bool is_quic(const endpoint& value) noexcept {
   return value.is_direct_quic();
}

[[nodiscard]] bool endpoint_rank_less(const endpoint& left, const endpoint& right) {
   const auto left_transport = is_quic(left) ? 0 : 1;
   const auto right_transport = is_quic(right) ? 0 : 1;
   if (left_transport != right_transport) {
      return left_transport < right_transport;
   }
   const auto left_family = is_ipv6(left) ? 0 : 1;
   const auto right_family = is_ipv6(right) ? 0 : 1;
   if (left_family != right_family) {
      return left_family < right_family;
   }
   if (left.transport.port != right.transport.port) {
      return left.transport.port < right.transport.port;
   }
   return left.to_string() < right.to_string();
}

[[nodiscard]] bool move_first_ipv4_second(std::vector<resolved_dial_target>& values, std::size_t begin,
                                           std::size_t end) {
   if (end - begin < 2 || !is_ipv6(values[begin].concrete)) {
      return false;
   }
   for (auto index = begin + 1; index < end; ++index) {
      if (!is_ipv4(values[index].concrete)) {
         continue;
      }
      std::rotate(values.begin() + static_cast<std::ptrdiff_t>(begin + 1),
                  values.begin() + static_cast<std::ptrdiff_t>(index),
                  values.begin() + static_cast<std::ptrdiff_t>(index + 1));
      return true;
   }
   return false;
}

[[nodiscard]] std::vector<dial_plan_item> rank_group(std::vector<resolved_dial_target> values,
                                                      std::chrono::milliseconds delay,
                                                      std::chrono::milliseconds tcp_handshake_progress_hold) {
   std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
      return endpoint_rank_less(left.concrete, right.concrete);
   });
   const auto first_tcp = static_cast<std::size_t>(
       std::distance(values.begin(), std::find_if(values.begin(), values.end(),
                                                  [](const auto& value) { return !is_quic(value.concrete); })));
   const auto quic_happy_eyeballs = move_first_ipv4_second(values, 0, first_tcp);
   const auto tcp_happy_eyeballs = move_first_ipv4_second(values, first_tcp, values.size());

   auto result = std::vector<dial_plan_item>{};
   result.reserve(values.size());
   auto tcp_first_delay = std::chrono::milliseconds::zero();
   for (auto index = std::size_t{0}; index < first_tcp; ++index) {
      auto scheduled = std::chrono::milliseconds::zero();
      if (index == 1) {
         scheduled = delay;
      } else if (index > 1) {
         scheduled = quic_happy_eyeballs ? delay * 2 : delay;
      }
      tcp_first_delay = scheduled + delay;
      result.push_back({.value = std::move(values[index].concrete),
                        .root_indices = std::move(values[index].root_indices),
                        .delay = scheduled});
   }
   for (auto index = first_tcp; index < values.size(); ++index) {
      const auto tcp_index = index - first_tcp;
      auto scheduled = tcp_first_delay;
      if (tcp_index == 1) {
         scheduled += delay;
      } else if (tcp_index > 1) {
         scheduled += tcp_happy_eyeballs ? delay * 2 : delay;
      }
      result.push_back({.value = std::move(values[index].concrete),
                        .root_indices = std::move(values[index].root_indices),
                        .delay = scheduled,
                        .tcp = true,
                        .tcp_handshake_progress_hold = tcp_handshake_progress_hold});
   }
   return result;
}

void validate_policy_value(const dialing::ranker_policy& value) {
   if (value.public_delay <= std::chrono::milliseconds::zero() || value.private_delay <= std::chrono::milliseconds::zero() ||
       value.public_delay > maximum_public_delay || value.private_delay > maximum_private_delay) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P dial ranker delay exceeds its donor policy");
   }
}

} // namespace

dial_ranker::dial_ranker(dialing::ranker_policy policy)
    : public_delay_(policy.public_delay), private_delay_(policy.private_delay) {
   validate_policy(policy);
}

void dial_ranker::validate_policy(const dialing::ranker_policy& value) {
   validate_policy_value(value);
}

std::vector<dial_plan_item> dial_ranker::rank(std::vector<resolved_dial_target> values) const {
   auto private_values = std::vector<resolved_dial_target>{};
   auto public_values = std::vector<resolved_dial_target>{};
   private_values.reserve(values.size());
   public_values.reserve(values.size());
   for (auto& value : values) {
      if (!is_supported_direct(value.concrete)) {
         continue;
      }
      const auto scope = host_addresses::classify_endpoint_scope(value.concrete);
      if (scope == host_addresses::endpoint_scope::public_address) {
         public_values.push_back(std::move(value));
      } else if (is_private_scope(scope)) {
         private_values.push_back(std::move(value));
      }
   }

   auto result = rank_group(std::move(private_values), private_delay_, std::chrono::milliseconds::zero());
   auto public_plan = rank_group(std::move(public_values), public_delay_, public_delay_);
   result.reserve(result.size() + public_plan.size());
   for (auto& item : public_plan) {
      result.push_back(std::move(item));
   }
   return result;
}

} // namespace forge::net::p2p::detail

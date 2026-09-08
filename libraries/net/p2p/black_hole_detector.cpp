module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.net.p2p.dialing;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;

#include "details/black_hole_detector.hxx"
#include "details/host_addresses.hxx"

namespace forge::net::p2p::detail {
namespace {

constexpr auto donor_window_size = std::size_t{100};
constexpr auto donor_min_successes = std::size_t{5};

[[nodiscard]] bool is_public(const endpoint& value) {
   return host_addresses::classify_endpoint_scope(value) == host_addresses::endpoint_scope::public_address;
}

[[nodiscard]] bool is_udp(const endpoint& value) noexcept {
   return value.is_direct_quic();
}

[[nodiscard]] bool is_ipv6(const endpoint& value) noexcept {
   return value.transport.host_type == endpoint::host_kind::ip6;
}

[[nodiscard]] std::size_t minimum_hardened_successes(std::size_t window_size) noexcept {
   return (window_size * donor_min_successes + donor_window_size - 1) / donor_window_size;
}

void validate_policy(const dialing::black_hole_policy& value) {
   if (value.window_size == 0 || value.window_size > donor_window_size || value.min_successes == 0 ||
       value.min_successes > value.window_size || value.min_successes < minimum_hardened_successes(value.window_size)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P black-hole policy exceeds its donor bounds");
   }
}

} // namespace

black_hole_detector::black_hole_detector(dialing::black_hole_policy policy)
    : udp_{.enabled = policy.udp_enabled, .window_size = policy.window_size, .min_successes = policy.min_successes},
      ipv6_{.enabled = policy.ipv6_enabled, .window_size = policy.window_size, .min_successes = policy.min_successes} {
   validate_policy(policy);
}

void black_hole_detector::reset(counter& value) noexcept {
   value.peer_requests = 0;
   value.successes = 0;
   value.outcomes.clear();
   value.state = dialing::black_hole_state::probing;
}

void black_hole_detector::update_state(counter& value) noexcept {
   if (value.outcomes.size() < value.window_size) {
      value.state = dialing::black_hole_state::probing;
   } else if (value.successes >= value.min_successes) {
      value.state = dialing::black_hole_state::allowed;
   } else {
      value.state = dialing::black_hole_state::blocked;
   }
}

void black_hole_detector::record(counter& value, dialing::outcome outcome) {
   if (!value.enabled || outcome == dialing::outcome::neutral) {
      return;
   }
   const auto success = outcome == dialing::outcome::success;
   if (value.state == dialing::black_hole_state::blocked && success) {
      reset(value);
      return;
   }
   if (success) {
      ++value.successes;
   }
   value.outcomes.push_back(success);
   if (value.outcomes.size() > value.window_size) {
      if (value.outcomes.front()) {
         --value.successes;
      }
      value.outcomes.erase(value.outcomes.begin());
   }
   update_state(value);
}

dialing::black_hole_state black_hole_detector::handle_request(counter& value) noexcept {
   if (!value.enabled) {
      return dialing::black_hole_state::allowed;
   }
   ++value.peer_requests;
   if (value.state == dialing::black_hole_state::allowed) {
      return dialing::black_hole_state::allowed;
   }
   if (value.state == dialing::black_hole_state::probing || value.peer_requests % value.window_size == 0) {
      return dialing::black_hole_state::probing;
   }
   return dialing::black_hole_state::blocked;
}

dialing::black_hole_counter_status black_hole_detector::snapshot(const counter& value) noexcept {
   auto result = dialing::black_hole_counter_status{
       .enabled = value.enabled,
       .state = value.state,
       .peer_requests = value.peer_requests,
       .outcomes = value.outcomes.size(),
       .successes = value.successes,
   };
   if (value.enabled && value.state == dialing::black_hole_state::blocked) {
      result.next_probe_after = value.window_size - value.peer_requests % value.window_size;
   }
   return result;
}

black_hole_filter_result black_hole_detector::filter_peer_dial(std::vector<endpoint> values) {
   const auto lock = std::lock_guard{mutex_};
   auto has_udp = false;
   auto has_ipv6 = false;
   for (const auto& value : values) {
      if (!is_public(value)) {
         continue;
      }
      has_udp = has_udp || is_udp(value);
      has_ipv6 = has_ipv6 || is_ipv6(value);
   }

   const auto udp_state = has_udp ? handle_request(udp_) : dialing::black_hole_state::allowed;
   const auto ipv6_state = has_ipv6 ? handle_request(ipv6_) : dialing::black_hole_state::allowed;
   auto result = black_hole_filter_result{};
   result.allowed.reserve(values.size());
   result.blocked.reserve(values.size());
   for (auto& value : values) {
      if (!is_public(value)) {
         result.allowed.push_back(std::move(value));
         continue;
      }
      // A probe for either dimension takes precedence over the other blocked dimension.
      if ((is_udp(value) && udp_state == dialing::black_hole_state::probing) ||
          (is_ipv6(value) && ipv6_state == dialing::black_hole_state::probing)) {
         result.allowed.push_back(std::move(value));
      } else if ((is_udp(value) && udp_state == dialing::black_hole_state::blocked) ||
                 (is_ipv6(value) && ipv6_state == dialing::black_hole_state::blocked)) {
         result.blocked.push_back(std::move(value));
      } else {
         result.allowed.push_back(std::move(value));
      }
   }
   return result;
}

void black_hole_detector::record_address_outcome(const endpoint& value, dialing::outcome outcome) {
   const auto lock = std::lock_guard{mutex_};
   if (outcome == dialing::outcome::neutral || !is_public(value)) {
      return;
   }
   if (is_udp(value)) {
      record(udp_, outcome);
   }
   if (is_ipv6(value)) {
      record(ipv6_, outcome);
   }
}

dialing::black_hole_status black_hole_detector::status() const {
   const auto lock = std::lock_guard{mutex_};
   return {.udp = snapshot(udp_), .ipv6 = snapshot(ipv6_)};
}

} // namespace forge::net::p2p::detail

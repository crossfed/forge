#pragma once

namespace forge::net::p2p::detail {

[[nodiscard]] constexpr bool peer_attributable_pubsub_failure(exceptions::code kind, bool stopped) noexcept {
   switch (kind) {
   case exceptions::code::invalid_options:
   case exceptions::code::invalid_identity:
   case exceptions::code::duplicate_protocol:
   case exceptions::code::backpressure_rejected:
   case exceptions::code::canceled:
   case exceptions::code::internal:
      return false;
   case exceptions::code::closed:
      return !stopped;
   default:
      return true;
   }
}

} // namespace forge::net::p2p::detail

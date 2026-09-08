module;

#include <atomic>

module forge.net.yamux.session;

#include "details/session_close_failure.hxx"

namespace forge::net::yamux::detail {
namespace {

std::atomic_bool session_close_failure{false};

} // namespace

void fail_next_session_close_for_test() noexcept {
   session_close_failure.store(true, std::memory_order_release);
}

bool consume_session_close_failure_for_test() noexcept {
   return session_close_failure.exchange(false, std::memory_order_acq_rel);
}

} // namespace forge::net::yamux::detail

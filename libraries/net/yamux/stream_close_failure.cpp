module;

#include <atomic>

module forge.net.yamux.session;

#include "details/stream_close_failure.hxx"

namespace forge::net::yamux::detail {
namespace {

std::atomic_bool stream_close_failure{false};

} // namespace

void fail_next_stream_close_for_test() noexcept {
   stream_close_failure.store(true, std::memory_order_release);
}

bool consume_stream_close_failure_for_test() noexcept {
   return stream_close_failure.exchange(false, std::memory_order_acq_rel);
}

} // namespace forge::net::yamux::detail

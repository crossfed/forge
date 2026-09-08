#pragma once

namespace forge::net::yamux::detail {

void fail_next_stream_close_for_test() noexcept;
[[nodiscard]] bool consume_stream_close_failure_for_test() noexcept;

} // namespace forge::net::yamux::detail

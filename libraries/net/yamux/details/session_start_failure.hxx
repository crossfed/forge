#pragma once

namespace forge::net::yamux::detail {

void fail_next_session_start_for_test() noexcept;
[[nodiscard]] bool consume_session_start_failure_for_test() noexcept;

} // namespace forge::net::yamux::detail

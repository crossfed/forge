#pragma once

#include <string>

namespace forge::asio::detail {

void set_current_thread_name(const std::string& name) noexcept;

} // namespace forge::asio::detail

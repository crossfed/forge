#pragma once

#include <functional>
#include <memory>
#include <stop_token>

namespace forge::asio::detail {

class stop_state {
 public:
   stop_state();
   ~stop_state();

   stop_state(const stop_state&) noexcept;
   stop_state& operator=(const stop_state&) noexcept;
   stop_state(stop_state&&) noexcept;
   stop_state& operator=(stop_state&&) noexcept;

   [[nodiscard]] bool request_stop() noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;
   [[nodiscard]] std::stop_token token() const noexcept;
   void link(std::stop_token parent, std::function<void()> callback);
   void clear_link() noexcept;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::asio::detail

module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/this_coro.hpp>

#include <functional>
#include <mutex>
#include <utility>

module forge.net.p2p.provider_registration;

import forge.asio.gate;

#include "details/provider_registration_impl.hxx"

namespace forge::net::p2p {

provider_registration::impl::impl(protocol_id profile_value, dht::key key_value, active_callback active_value,
                                  request_stop_callback request_stop_value, withdraw_callback withdraw_value)
    : profile(std::move(profile_value)), key(std::move(key_value)), active_callback_value(std::move(active_value)),
      request_stop_callback_value(std::move(request_stop_value)), withdraw_callback_value(std::move(withdraw_value)) {}

bool provider_registration::impl::active() const noexcept {
   auto callback = active_callback{};
   {
      auto lock = std::scoped_lock{mutex};
      if (stop_requested || withdrawn) {
         return false;
      }
      callback = active_callback_value;
   }
   try {
      return callback && callback();
   } catch (...) {
      return false;
   }
}

void provider_registration::impl::request_stop() noexcept {
   auto callback = request_stop_callback{};
   {
      auto lock = std::scoped_lock{mutex};
      if (stop_requested || withdrawn) {
         return;
      }
      stop_requested = true;
      callback = std::move(request_stop_callback_value);
   }
   try {
      if (callback) {
         callback();
      }
   } catch (...) {
   }
}

boost::asio::awaitable<void> provider_registration::impl::async_withdraw() {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   request_stop();
   auto ticket = co_await withdraw_gate.acquire();
   auto callback = withdraw_callback{};
   {
      auto lock = std::scoped_lock{mutex};
      if (withdrawn) {
         co_return;
      }
      callback = withdraw_callback_value;
   }
   if (callback) {
      co_await callback();
   }
   auto lock = std::scoped_lock{mutex};
   withdrawn = true;
   active_callback_value = {};
   request_stop_callback_value = {};
   withdraw_callback_value = {};
}

boost::asio::awaitable<void> provider_registration::impl::async_withdraw_owned(std::shared_ptr<impl> self) {
   co_await self->async_withdraw();
}

} // namespace forge::net::p2p

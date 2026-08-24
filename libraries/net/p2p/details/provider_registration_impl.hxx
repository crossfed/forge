#pragma once

#include <mutex>

namespace forge::net::p2p {

struct provider_registration::impl {
   impl(protocol_id profile_value, dht::key key_value, active_callback active_value,
        request_stop_callback request_stop_value, withdraw_callback withdraw_value);

   [[nodiscard]] bool active() const noexcept;
   void request_stop() noexcept;
   boost::asio::awaitable<void> async_withdraw();
   static boost::asio::awaitable<void> async_withdraw_owned(std::shared_ptr<impl> self);

   protocol_id profile;
   dht::key key;
   active_callback active_callback_value;
   request_stop_callback request_stop_callback_value;
   withdraw_callback withdraw_callback_value;
   forge::asio::gate withdraw_gate;
   mutable std::mutex mutex;
   bool stop_requested = false;
   bool withdrawn = false;
};

} // namespace forge::net::p2p

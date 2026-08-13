#pragma once

namespace forge::plugins::p2p::resolver::detail {

class managed_remote_invoker final : public forge::api::core::remote_invoker,
                                     public std::enable_shared_from_this<managed_remote_invoker> {
 public:
   managed_remote_invoker(std::weak_ptr<plugin::impl> owner, std::vector<forge::net::p2p::peer_id> ordered_peers,
                          forge::api::core::api_ref requested, forge::api::core::descriptor descriptor,
                          managed_remote_options options, std::size_t max_waiters);
   ~managed_remote_invoker() override;

   boost::asio::awaitable<void> connect_initial();
   void request_stop() noexcept;
   boost::asio::awaitable<void> async_stop();

   boost::asio::awaitable<forge::api::core::response> async_call(forge::api::core::request value) override;
   boost::asio::awaitable<forge::api::core::response>
   async_stream_call(forge::api::core::request value, forge::api::core::method_kind kind,
                     std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
                     std::shared_ptr<forge::api::core::detail::stream_endpoint> output) override;

 private:
   struct generation;
   struct reconnect_flight;
   struct timer_state;

   [[nodiscard]] boost::asio::awaitable<std::shared_ptr<generation>> require_generation();
   boost::asio::awaitable<void> run_connect(std::shared_ptr<reconnect_flight> flight);
   [[nodiscard]] boost::asio::awaitable<std::shared_ptr<generation>> connect_generation();
   [[nodiscard]] std::chrono::milliseconds backoff_for(std::uint32_t round) const noexcept;
   void invalidate(const std::shared_ptr<generation>& value) noexcept;
   void leave_flight(const std::shared_ptr<reconnect_flight>& value) noexcept;
   [[nodiscard]] bool stopped() const noexcept;

   std::weak_ptr<plugin::impl> owner_;
   std::vector<forge::net::p2p::peer_id> peers_;
   forge::api::core::api_ref requested_;
   forge::api::core::descriptor descriptor_;
   managed_remote_options options_;
   std::size_t max_waiters_ = 0;
   mutable std::mutex mutex_;
   std::shared_ptr<generation> current_;
   std::shared_ptr<reconnect_flight> reconnect_;
   std::shared_ptr<timer_state> backoff_timer_;
   std::size_t next_peer_ = 0;
   bool stopped_ = false;
};

} // namespace forge::plugins::p2p::resolver::detail

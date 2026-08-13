#pragma once

#include "frame.hxx"

namespace forge::net::yamux {

struct session::impl final : std::enable_shared_from_this<session::impl> {
   struct stream_state {
      explicit stream_state(std::uint32_t stream_id);

      std::uint32_t id = 0;
      std::uint32_t send_window = 0;
      std::uint32_t receive_window = 0;
      std::uint32_t pending_receive_credit = 0;
      std::deque<detail::bytes> inbound;
      std::size_t buffered = 0;
      bool local_fin = false;
      bool remote_fin = false;
      bool reset = false;
      bool accepted = false;
      forge::asio::notification read_notification;
      forge::asio::notification window_notification;
      forge::asio::notification receive_credit_notification;
   };

   class stream_model;

   impl(transport::stream stream, side session_side, options session_options);

   [[nodiscard]] bool valid() const noexcept;
   boost::asio::awaitable<void> ensure_started();
   boost::asio::awaitable<transport::stream> async_open_stream();
   boost::asio::awaitable<transport::stream> async_accept_stream();
   boost::asio::awaitable<void> async_close();
   void cancel();

   boost::asio::awaitable<void> write_stream(const std::shared_ptr<stream_state>& state,
                                             detail::bytes payload,
                                             std::shared_ptr<void> lifetime = {});
   boost::asio::awaitable<detail::bytes> read_stream(const std::shared_ptr<stream_state>& state);
   boost::asio::awaitable<void> close_stream(const std::shared_ptr<stream_state>& state);
   void cancel_stream(const std::shared_ptr<stream_state>& state);

 private:
   [[nodiscard]] static bool exceeds_limit(std::size_t current, std::size_t addition,
                                           std::size_t limit) noexcept;
   [[nodiscard]] static bool remote_opens_stream(side local_side, std::uint32_t stream_id) noexcept;
   void validate_options() const;
   [[nodiscard]] std::uint32_t local_window_delta() const noexcept;
   [[nodiscard]] std::uint32_t checked_peer_window(std::uint32_t current,
                                                   std::uint32_t delta) const;
   [[nodiscard]] std::shared_ptr<stream_state> make_stream_locked(std::uint32_t id,
                                                                 std::uint32_t send_window);
   [[nodiscard]] std::shared_ptr<stream_state> get_stream_locked(std::uint32_t id) const;
   void require_stream_owned_locked(const std::shared_ptr<stream_state>& state) const;
   [[nodiscard]] bool stream_valid(const std::shared_ptr<stream_state>& state) const noexcept;
   [[nodiscard]] transport::stream make_transport_stream(const std::shared_ptr<stream_state>& state);

   void rethrow_terminal_locked() const;
   [[nodiscard]] bool mark_closing();
   void fail_session(exceptions::code value, std::string message);
   void wake_all_locked();

   boost::asio::awaitable<bool> write_prepared(std::function<std::optional<detail::bytes>()> prepare,
                                               bool allow_after_close = false,
                                               std::shared_ptr<void> lifetime = {});
   boost::asio::awaitable<void> write_frame(detail::frame_type type, std::uint16_t flags,
                                            std::uint32_t stream_id, std::uint32_t length,
                                            std::span<const std::uint8_t> payload = {},
                                            bool allow_after_close = false,
                                            std::shared_ptr<void> lifetime = {});

   boost::asio::awaitable<void> read_loop();
   boost::asio::awaitable<void> wait_for_read_loop();
   boost::asio::awaitable<bool> wait_for_read_loop_until(std::chrono::steady_clock::time_point deadline);
   void finish_read_loop();
   static void compact_read_buffer(detail::bytes& buffer, std::size_t& consumed);
   boost::asio::awaitable<std::pair<detail::frame_header, detail::bytes>>
   read_frame(detail::bytes& buffer, std::size_t& consumed);
   boost::asio::awaitable<void> handle_frame(const detail::frame_header& header,
                                             const detail::bytes& payload);
   boost::asio::awaitable<void> handle_data(const detail::frame_header& header,
                                            const detail::bytes& payload);
   boost::asio::awaitable<void> handle_window_update(const detail::frame_header& header);
   boost::asio::awaitable<std::shared_ptr<stream_state>>
   handle_stream_open(const detail::frame_header& header, std::uint32_t send_window);
   boost::asio::awaitable<void> handle_ping(const detail::frame_header& header);
   [[noreturn]] void handle_go_away(const detail::frame_header& header);

   [[nodiscard]] bool is_reclaimable_stream_locked(const stream_state& state) const noexcept;
   void reclaim_closed_streams_locked();
   void reset_stream(std::uint32_t id);
   void reset_stream_locked(const std::shared_ptr<stream_state>& state);
   void release_stream_buffers_locked(stream_state& state);
   static void notify_stream_waiters_locked(const std::shared_ptr<stream_state>& state);

   transport::stream stream_;
   side side_ = side::initiator;
   options options_;

   mutable std::mutex mutex_;
   std::optional<boost::asio::any_io_executor> executor_;
   forge::asio::notification accept_notification_;
   forge::asio::notification read_loop_notification_;
   forge::asio::gate write_gate_;
   std::map<std::uint32_t, std::shared_ptr<stream_state>> streams_;
   std::deque<std::uint32_t> pending_accepts_;
   std::size_t session_buffer_ = 0;
   std::optional<std::uint32_t> next_stream_id_;
   bool started_ = false;
   bool read_loop_done_ = false;
   bool closed_ = false;
   bool canceled_ = false;
   std::exception_ptr terminal_error_;
};

} // namespace forge::net::yamux

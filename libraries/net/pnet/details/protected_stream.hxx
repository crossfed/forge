#pragma once

namespace forge::net::pnet::detail {

class protected_stream final : public forge::net::transport::detail::stream_concept,
                               public std::enable_shared_from_this<protected_stream> {
 public:
   [[nodiscard]] static forge::net::transport::stream_connection
   wrap(forge::net::transport::stream_connection connection, std::shared_ptr<const pre_shared_key> key,
        std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size> local_nonce,
        boost::asio::any_io_executor executor);

   ~protected_stream() override;

   [[nodiscard]] bool valid() const noexcept override;
   [[nodiscard]] std::int64_t id() const noexcept override;
   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override;
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override;
   boost::asio::awaitable<void> async_close() override;
   void cancel() override;

 private:
   enum class terminal_state : std::uint8_t {
      active,
      closing,
      cancel_requested,
      completed,
   };

   protected_stream(forge::net::transport::stream stream, std::shared_ptr<const pre_shared_key> key,
                    const std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size>& local_nonce,
                    boost::asio::any_io_executor executor);

   void require_active() const;
   void begin_close() noexcept;
   void request_cancel() noexcept;
   void request_lower_cancel() noexcept;
   void start_terminal_worker() noexcept;
   boost::asio::awaitable<void>
   async_terminal_worker(std::shared_ptr<forge::net::transport::stream> lower);
   boost::asio::awaitable<void> wait_for_terminal();
   void finish_terminal(std::exception_ptr error) noexcept;
   [[nodiscard]] std::shared_ptr<forge::net::transport::stream> lower_stream() const;
   [[nodiscard]] std::unique_ptr<forge::crypto::symmetric::xsalsa20::stream>
   make_cipher(const std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size>& nonce) const;

   mutable std::mutex lower_mutex_;
   std::shared_ptr<forge::net::transport::stream> lower_stream_;
   std::shared_ptr<const pre_shared_key> key_;
   boost::asio::any_io_executor executor_;
   forge::asio::gate read_gate_;
   forge::asio::gate write_gate_;
   std::vector<std::uint8_t> pending_peer_nonce_;
   std::unique_ptr<forge::crypto::symmetric::xsalsa20::stream> read_cipher_;
   std::unique_ptr<forge::crypto::symmetric::xsalsa20::stream> write_cipher_;
   std::atomic<terminal_state> terminal_{terminal_state::active};
   std::atomic_bool lower_cancel_requested_ = false;
   std::atomic_bool terminal_worker_started_ = false;
   mutable std::mutex terminal_mutex_;
   forge::asio::notification terminal_notification_;
   std::exception_ptr terminal_error_;
   bool terminal_done_ = false;
};

} // namespace forge::net::pnet::detail

#pragma once

#include "transport_stream_adapter.hxx"

namespace forge::net::stcp::detail {

class stream_backend {
 public:
   virtual ~stream_backend() = default;

   [[nodiscard]] virtual boost::asio::any_io_executor get_executor() const noexcept = 0;
   [[nodiscard]] virtual bool is_open() const noexcept = 0;
   [[nodiscard]] virtual SSL* native_handle() const noexcept = 0;
   virtual boost::asio::awaitable<boost::system::error_code>
   async_handshake(boost::asio::ssl::stream_base::handshake_type type) = 0;
   virtual boost::asio::awaitable<boost::system::error_code> async_write(std::span<const std::uint8_t> bytes) = 0;
   virtual boost::asio::awaitable<std::pair<boost::system::error_code, std::size_t>>
   async_read_some(std::span<std::uint8_t> bytes) = 0;
   virtual void request_cancel() noexcept = 0;
   virtual boost::asio::awaitable<void> async_terminal_close() = 0;
};

class native_stream_backend final : public stream_backend {
 public:
   explicit native_stream_backend(std::shared_ptr<forge::net::tls::asio_tls_stream> stream);

   [[nodiscard]] boost::asio::any_io_executor get_executor() const noexcept override;
   [[nodiscard]] bool is_open() const noexcept override;
   [[nodiscard]] SSL* native_handle() const noexcept override;
   boost::asio::awaitable<boost::system::error_code>
   async_handshake(boost::asio::ssl::stream_base::handshake_type type) override;
   boost::asio::awaitable<boost::system::error_code> async_write(std::span<const std::uint8_t> bytes) override;
   boost::asio::awaitable<std::pair<boost::system::error_code, std::size_t>>
   async_read_some(std::span<std::uint8_t> bytes) override;
   void request_cancel() noexcept override;
   boost::asio::awaitable<void> async_terminal_close() override;

 private:
   std::shared_ptr<forge::net::tls::asio_tls_stream> stream_;
};

class transport_stream_backend final : public stream_backend {
 public:
   using tls_stream = boost::asio::ssl::stream<transport_stream_adapter>;

   explicit transport_stream_backend(std::shared_ptr<tls_stream> stream);

   [[nodiscard]] boost::asio::any_io_executor get_executor() const noexcept override;
   [[nodiscard]] bool is_open() const noexcept override;
   [[nodiscard]] SSL* native_handle() const noexcept override;
   boost::asio::awaitable<boost::system::error_code>
   async_handshake(boost::asio::ssl::stream_base::handshake_type type) override;
   boost::asio::awaitable<boost::system::error_code> async_write(std::span<const std::uint8_t> bytes) override;
   boost::asio::awaitable<std::pair<boost::system::error_code, std::size_t>>
   async_read_some(std::span<std::uint8_t> bytes) override;
   void request_cancel() noexcept override;
   boost::asio::awaitable<void> async_terminal_close() override;

 private:
   std::shared_ptr<tls_stream> stream_;
};

[[nodiscard]] std::shared_ptr<stream_backend>
make_native_stream_backend(std::shared_ptr<forge::net::tls::asio_tls_stream> stream);
[[nodiscard]] std::shared_ptr<stream_backend>
make_transport_stream_backend(boost::asio::any_io_executor executor, forge::net::transport::stream stream,
                              forge::net::tls::context_snapshot_ptr context);

} // namespace forge::net::stcp::detail

#pragma once

namespace forge::net::p2p::detail {

class private_transport_stream final : public forge::net::transport::detail::stream_concept {
 public:
   explicit private_transport_stream(forge::net::transport::stream stream);
   ~private_transport_stream() override;

   [[nodiscard]] bool valid() const noexcept override;
   [[nodiscard]] std::int64_t id() const noexcept override;
   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override;
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override;
   boost::asio::awaitable<void> async_close() override;
   void cancel() override;

 private:
   forge::net::transport::stream stream_;
};

[[nodiscard]] forge::net::transport::stream
adapt_private_transport_stream(forge::net::transport::stream stream);

} // namespace forge::net::p2p::detail

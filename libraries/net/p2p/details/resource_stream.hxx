#pragma once

namespace forge::net::p2p::detail {

class resource_stream final : public forge::net::transport::detail::stream_concept {
 public:
   resource_stream(forge::net::transport::stream stream, resource_manager manager,
                   resource_manager::stream_reservation reservation);
   ~resource_stream() override;

   [[nodiscard]] bool valid() const noexcept override;
   [[nodiscard]] std::int64_t id() const noexcept override;
   [[nodiscard]] bool bind(resource_manager::scope value) noexcept;

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override;
   boost::asio::awaitable<void> async_write_chunk(forge::net::transport::chunk bytes) override;
   boost::asio::awaitable<void> async_write_frame(std::span<const std::uint8_t> bytes) override;
   boost::asio::awaitable<void> async_write_frame_chunk(forge::net::transport::chunk bytes) override;
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override;
   boost::asio::awaitable<forge::net::transport::chunk> async_read_chunk() override;
   boost::asio::awaitable<void> async_close() override;
   void cancel() override;

 private:
   forge::net::transport::stream stream_;
   resource_manager manager_;
   resource_manager::stream_reservation reservation_;
};

[[nodiscard]] std::pair<forge::net::transport::stream, std::shared_ptr<resource_stream>>
make_resource_stream(forge::net::transport::stream stream, resource_manager manager,
                     resource_manager::stream_reservation reservation);

boost::asio::awaitable<void>
async_close_unescaped(const std::shared_ptr<resource_stream>& resource);

} // namespace forge::net::p2p::detail

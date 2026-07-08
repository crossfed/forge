module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <vector>

export module forge.api.transport.client;

export import forge.api.core.types;
export import forge.api.transport.exceptions;
export import forge.api.transport.options;
export import forge.net.transport.stream;

export namespace forge::api::transport {

class client {
 public:
   client();
   client(forge::net::transport::stream stream, options value = {});
   ~client();

   client(client&&) noexcept;
   client& operator=(client&&) noexcept;

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const options& settings() const noexcept;

   boost::asio::awaitable<forge::api::core::frame> async_call(forge::api::core::frame request, call_options value = {});
   boost::asio::awaitable<std::vector<forge::api::core::frame>> async_call_stream(forge::api::core::frame request,
                                                                          call_options value = {});
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::api::transport

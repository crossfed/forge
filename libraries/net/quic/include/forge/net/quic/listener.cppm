module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.net.quic.listener;

import forge.asio.runtime;
import forge.net.quic.endpoint;
import forge.net.quic.options;
export import forge.net.quic.connection;

export namespace forge::net::quic {

class listener {
 public:
   listener(forge::asio::runtime& runtime, endpoint bind_endpoint, server_options options);
   ~listener();

   listener(const listener&) = delete;
   listener& operator=(const listener&) = delete;

   [[nodiscard]] endpoint local_endpoint() const;
   boost::asio::awaitable<connection> async_accept();
   void stop();

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::net::quic

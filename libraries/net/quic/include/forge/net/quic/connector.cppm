module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.net.quic.connector;

import forge.asio.runtime;
import forge.net.quic.endpoint;
import forge.net.quic.options;
export import forge.net.quic.connection;

export namespace forge::net::quic {

class connector {
 public:
   explicit connector(forge::asio::runtime& runtime);
   ~connector();

   connector(const connector&) = delete;
   connector& operator=(const connector&) = delete;

   boost::asio::awaitable<connection> async_connect(endpoint remote, client_options options = {});
   void cancel();

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::net::quic

#pragma once

#include "resolver_protocol.hxx"

namespace forge::plugins::p2p::resolver {

class plugin::resolver_protocol_service final : public detail::resolver_protocol {
 public:
   explicit resolver_protocol_service(std::shared_ptr<plugin::impl> impl);
   boost::asio::awaitable<response> query(
      ::forge::plugins::p2p::resolver::query request) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace forge::plugins::p2p::resolver

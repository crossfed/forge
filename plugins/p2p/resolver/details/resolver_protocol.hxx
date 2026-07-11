#pragma once

namespace forge::plugins::p2p::resolver::detail {

class resolver_protocol
    : public forge::api::core::contract<resolver_protocol,
                                        forge::api::core::surface::local |
                                           forge::api::core::surface::remote> {
 public:
   virtual ~resolver_protocol() = default;
   virtual boost::asio::awaitable<response> query(
      ::forge::plugins::p2p::resolver::query request) = 0;
};

} // namespace forge::plugins::p2p::resolver::detail

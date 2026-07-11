#pragma once

namespace forge::plugins::p2p::resolver::detail {

class resolver_protocol
    : public forge::api::core::contract<resolver_protocol,
                                        forge::api::core::surface::local |
                                           forge::api::core::surface::remote> {
 public:
   explicit resolver_protocol(std::shared_ptr<plugin::impl> owner)
      : owner_{std::move(owner)} {}

   virtual ~resolver_protocol() = default;

   virtual boost::asio::awaitable<response> query(
      ::forge::plugins::p2p::resolver::query request) {
      co_return owner_->query_local(request);
   }

 protected:
   resolver_protocol() = default;

 private:
   std::shared_ptr<plugin::impl> owner_;
};

} // namespace forge::plugins::p2p::resolver::detail

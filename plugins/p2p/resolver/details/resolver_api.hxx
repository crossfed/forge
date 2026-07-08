#pragma once

namespace forge::plugins::p2p::resolver {

class plugin::resolver_api final : public api {
 public:
   explicit resolver_api(std::shared_ptr<plugin::impl> impl);

   void publish_api(forge::api::core::binding_plan plan, forge::net::p2p::protocol_id protocol,
                    publish_options options) override;
   [[nodiscard]] std::vector<entry> local_apis() const override;
   boost::asio::awaitable<std::vector<entry>> peer_apis(forge::net::p2p::peer_id peer,
                                                        resolve_options options) override;
   boost::asio::awaitable<resolution> resolve(forge::net::p2p::peer_id peer, forge::api::core::api_ref api,
                                              resolve_options options) override;

 private:
   boost::asio::awaitable<resolved_connection>
   open_resolved_connection(forge::net::p2p::peer_id peer, forge::api::core::api_ref api,
                            forge::api::core::descriptor descriptor, resolve_options options) override;

   std::shared_ptr<plugin::impl> impl_;
};

} // namespace forge::plugins::p2p::resolver

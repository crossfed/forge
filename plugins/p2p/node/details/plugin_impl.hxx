#pragma once

#include <mutex>

#include "bootstrap_peer.hxx"
#include "config.hxx"

namespace forge::plugins::p2p::node {

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   forge::net::p2p::node::options options{
       .allow_insecure_test_mode = false,
   };
   forge::api::transport::options api_options{
       .codec = forge::api::core::codec_id{.value = "forge.raw"},
       .max_inflight = 64,
   };
   parsed_policy policy{};
   std::vector<forge::net::p2p::endpoint> listen;
   std::vector<bootstrap_peer> bootstrap;
   std::vector<std::pair<forge::net::p2p::protocol_id, forge::net::p2p::node::protocol_handler>> routes;
   forge::net::p2p::pubsub::options pubsub_options{};
   std::unique_ptr<forge::net::p2p::node> node;
   forge::asio::runtime* runtime = nullptr;
   forge::asio::task::scheduler* scheduler = nullptr;
   std::mutex bootstrap_maintenance_mutex;
   forge::asio::task::handle bootstrap_maintenance;
   bool pubsub_requested = false;
   bool started = false;
   std::atomic_bool stopping{false};

   [[nodiscard]] forge::net::p2p::node& ensure_node();
   [[nodiscard]] forge::net::p2p::node& require_node();
   [[nodiscard]] const forge::net::p2p::node& require_node() const;
   boost::asio::awaitable<void> refresh_bootstrap();
   void start_bootstrap_maintenance();
   void request_bootstrap_stop() noexcept;
   boost::asio::awaitable<void> stop_bootstrap_maintenance();
   void add_route(forge::net::p2p::protocol_id protocol, forge::net::p2p::node::protocol_handler handler);
   [[nodiscard]] forge::net::p2p::node::open_options open_options_for(remote_options value) const;
   [[nodiscard]] forge::api::transport::options api_options_for(const remote_options& value) const;
};

} // namespace forge::plugins::p2p::node

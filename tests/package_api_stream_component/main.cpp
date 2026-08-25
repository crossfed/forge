#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstdint>
#include <utility>

import forge.api.stream.options;
import forge.api.stream.server;
import forge.api.stream.session;
import forge.api.core.trusted_invocation;

int main() {
   auto options = forge::api::stream::options{};
   auto session = forge::api::stream::session{};
   auto legacy_dispatch = forge::api::core::dispatch_options{
      forge::api::core::codec_id{.value = "forge.raw"},
      128,
      std::chrono::milliseconds{0},
      forge::api::core::metadata{},
   };
   auto [codec, max_inflight, deadline, trusted_metadata] =
      forge::api::core::dispatch_options{};
   auto legacy_session = [](forge::net::transport::stream stream,
                            forge::api::core::binding_plan plan) {
      return forge::api::stream::session{
         std::move(stream), std::move(plan), forge::api::stream::options{}, {}};
   };
   auto legacy_server = [](forge::net::transport::stream stream,
                           forge::api::core::binding_plan plan)
      -> boost::asio::awaitable<void> {
      co_await forge::api::stream::serve_stream(
         std::move(stream), std::move(plan), forge::api::stream::options{}, {});
   };
   using legacy_server_type = boost::asio::awaitable<void> (*)(
      forge::net::transport::stream, forge::api::core::binding_plan,
      forge::api::stream::options, forge::api::core::metadata);
   auto legacy_server_pointer = static_cast<legacy_server_type>(
      &forge::api::stream::serve_stream);
   (void)options;
   (void)session;
   (void)legacy_dispatch;
   (void)codec;
   (void)max_inflight;
   (void)deadline;
   (void)trusted_metadata;
   (void)legacy_session;
   (void)legacy_server;
   (void)legacy_server_pointer;
   return 0;
}

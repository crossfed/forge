module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <utility>

export module forge.api.quic.binding;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.api.transport.exceptions;
import forge.api.transport.options;
import forge.api.transport.client;
import forge.api.transport.connection;
import forge.api.transport.server;
import forge.net.quic.stream;
import forge.net.quic.transport;
import forge.net.transport.stream;

export namespace forge::api::quic {

class api_binding {
 public:
   api_binding(forge::api::core::binding_plan plan, forge::api::transport::options options)
       : plan_{std::move(plan)}, options_{std::move(options)} {}

   boost::asio::awaitable<void> accept(forge::net::transport::stream stream) const {
      co_await forge::api::transport::serve_stream(std::move(stream), plan_, options_);
   }

   boost::asio::awaitable<void> accept(forge::net::quic::stream stream) const {
      co_await accept(forge::net::quic::as_transport_stream(std::move(stream)));
   }

   boost::asio::awaitable<void> connect(forge::net::transport::stream stream) const {
      co_await accept(std::move(stream));
   }

   boost::asio::awaitable<void> connect(forge::net::quic::stream stream) const {
      co_await accept(std::move(stream));
   }

   [[nodiscard]] const forge::api::core::codec_id& codec() const noexcept {
      return options_.codec;
   }

   [[nodiscard]] std::size_t max_concurrent_calls() const noexcept {
      return options_.max_inflight;
   }

   [[nodiscard]] std::chrono::milliseconds deadline() const noexcept {
      return options_.deadline;
   }

   [[nodiscard]] const forge::api::transport::options& options() const noexcept {
      return options_;
   }

 private:
   forge::api::core::binding_plan plan_;
   forge::api::transport::options options_;
};

class api_builder {
 public:
   api_builder& use(forge::api::core::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& codec(forge::api::core::codec_id value) {
      options_.codec = std::move(value);
      return *this;
   }

   api_builder& max_concurrent_calls(std::size_t value) {
      options_.max_inflight = value;
      return *this;
   }

   api_builder& deadline(std::chrono::milliseconds value) {
      options_.deadline = value;
      return *this;
   }

   api_builder& max_frame_size(std::uint32_t value) {
      options_.max_frame_size = value;
      return *this;
   }

   [[nodiscard]] api_binding build() {
      return api_binding{std::move(plan_), options_};
   }

 private:
   forge::api::core::binding_plan plan_;
   forge::api::transport::options options_{.deadline = std::chrono::milliseconds{5000}};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace forge::api::quic

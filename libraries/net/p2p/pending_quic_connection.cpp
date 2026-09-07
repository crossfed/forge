module;

#include <mutex>
#include <optional>
#include <utility>

module forge.net.p2p.node;

import forge.net.quic.connection;

#include "details/pending_quic_connection.hxx"

namespace forge::net::p2p::direct::detail {

void pending_quic_connection::install(forge::net::quic::connection value) noexcept {
   const auto lock = std::scoped_lock{mutex_};
   value_.emplace(std::move(value));
}

forge::net::quic::connection* pending_quic_connection::get() noexcept {
   const auto lock = std::scoped_lock{mutex_};
   return value_ ? &*value_ : nullptr;
}

forge::net::quic::connection pending_quic_connection::take() noexcept {
   const auto lock = std::scoped_lock{mutex_};
   if (!value_) {
      return {};
   }
   auto result = std::move(*value_);
   value_.reset();
   return result;
}

void pending_quic_connection::request_cancel() noexcept {
   const auto lock = std::scoped_lock{mutex_};
   if (value_) {
      value_->request_cancel();
   }
}

} // namespace forge::net::p2p::direct::detail

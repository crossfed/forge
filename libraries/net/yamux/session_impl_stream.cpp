module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.yamux.session;

import forge.asio.gate;
import forge.asio.notification;

#include "details/session_impl.hxx"

namespace forge::net::yamux {

boost::asio::awaitable<transport::stream> session::impl::async_open_stream() {
   co_await ensure_started();

   std::shared_ptr<stream_state> state;
   co_await write_prepared([this, &state]() -> std::optional<detail::bytes> {
      auto lock = std::scoped_lock{mutex_};
      rethrow_terminal_locked();
      reclaim_closed_streams_locked();
      if (streams_.size() >= options_.max_streams) {
         FORGE_THROW_EXCEPTION(exceptions::resource_limit, "yamux maximum stream count reached");
      }
      if (!next_stream_id_) {
         FORGE_THROW_EXCEPTION(exceptions::resource_limit, "yamux stream ids are exhausted");
      }
      const auto id = *next_stream_id_;
      auto encoded = detail::encode_frame(detail::frame_type::window_update, detail::syn, id,
                                          local_window_delta());
      auto prepared = make_stream_locked(id, detail::initial_stream_window);
      prepared->accepted = true;
      streams_.emplace(id, prepared);
      next_stream_id_ = detail::can_advance_stream_id(id)
                            ? std::optional<std::uint32_t>{id + 2U}
                            : std::nullopt;
      state = std::move(prepared);
      return encoded;
   });
   co_return make_transport_stream(state);
}

boost::asio::awaitable<transport::stream> session::impl::async_accept_stream() {
   co_await ensure_started();

   while (true) {
      const auto observed = accept_notification_.epoch();
      auto state = std::shared_ptr<stream_state>{};
      {
         auto lock = std::scoped_lock{mutex_};
         if (!pending_accepts_.empty()) {
            const auto id = pending_accepts_.front();
            pending_accepts_.pop_front();
            if (const auto found = streams_.find(id); found != streams_.end()) {
               found->second->accepted = true;
               state = found->second;
            }
         } else {
            rethrow_terminal_locked();
         }
      }
      if (state) {
         co_return make_transport_stream(state);
      }
      (void)co_await accept_notification_.async_wait(observed);
   }
}

boost::asio::awaitable<void> session::impl::write_stream(const std::shared_ptr<stream_state>& state,
                                                        detail::bytes payload,
                                                        std::shared_ptr<void> lifetime) {
   co_await ensure_started();

   auto offset = std::size_t{0};
   while (offset < payload.size()) {
      {
         auto lock = std::scoped_lock{mutex_};
         require_stream_owned_locked(state);
         if (state->local_fin) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "yamux stream is locally closed");
         }
      }

      while (true) {
         const auto observed = state->window_notification.epoch();
         {
            auto lock = std::scoped_lock{mutex_};
            require_stream_owned_locked(state);
            rethrow_terminal_locked();
            if (state->send_window > 0) {
               break;
            }
         }
         (void)co_await state->window_notification.async_wait(observed);
      }

      auto written = std::size_t{0};
      const auto sent = co_await write_prepared(
          [this, state, &payload, &offset, &written]() -> std::optional<detail::bytes> {
             auto lock = std::scoped_lock{mutex_};
             require_stream_owned_locked(state);
             rethrow_terminal_locked();
             if (state->local_fin) {
                FORGE_THROW_EXCEPTION(exceptions::closed, "yamux stream is locally closed");
             }
             if (state->send_window == 0) {
                return std::nullopt;
             }
             written = std::min<std::size_t>(
                 {payload.size() - offset, options_.max_frame_size, state->send_window});
             const auto chunk = std::span<const std::uint8_t>{payload.data() + offset, written};
             auto encoded = detail::encode_frame(detail::frame_type::data, 0, state->id,
                                                 static_cast<std::uint32_t>(written), chunk);
             state->send_window -= static_cast<std::uint32_t>(written);
             return encoded;
          },
          false, lifetime);
      if (sent) {
         offset += written;
      }
   }
}

boost::asio::awaitable<detail::bytes>
session::impl::read_stream(const std::shared_ptr<stream_state>& state) {
   co_await ensure_started();

   while (true) {
      const auto observed = state->read_notification.epoch();
      auto has_data = false;
      {
         auto lock = std::scoped_lock{mutex_};
         require_stream_owned_locked(state);
         if (!state->inbound.empty()) {
            has_data = true;
         } else if (state->remote_fin) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "yamux stream closed by remote");
         } else {
            rethrow_terminal_locked();
         }
      }

      if (has_data) {
         auto consumed = std::uint32_t{0};
         auto out = detail::bytes{};
         auto credit_pending = false;
         auto sent = false;
         try {
            sent = co_await write_prepared(
                [this, state, &consumed, &out, &credit_pending]() -> std::optional<detail::bytes> {
                   auto lock = std::scoped_lock{mutex_};
                   require_stream_owned_locked(state);
                   rethrow_terminal_locked();
                   if (state->inbound.empty()) {
                      return std::nullopt;
                   }
                   consumed = static_cast<std::uint32_t>(state->inbound.front().size());
                   if (state->receive_window > options_.initial_window ||
                       state->pending_receive_credit > options_.initial_window - state->receive_window ||
                       consumed > options_.initial_window - state->receive_window -
                                      state->pending_receive_credit) {
                      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                                            "yamux receive window accounting overflow");
                   }
                   auto encoded = detail::encode_frame(detail::frame_type::window_update, 0, state->id, consumed);
                   out = std::move(state->inbound.front());
                   state->inbound.pop_front();
                   state->buffered -= out.size();
                   session_buffer_ -= out.size();
                   state->pending_receive_credit += consumed;
                   credit_pending = true;
                   return encoded;
                });
         } catch (...) {
            if (credit_pending) {
               auto lock = std::scoped_lock{mutex_};
               state->pending_receive_credit -= consumed;
               state->receive_credit_notification.notify();
            }
            throw;
         }
         if (!sent) {
            continue;
         }
         {
            auto lock = std::scoped_lock{mutex_};
            state->pending_receive_credit -= consumed;
            state->receive_window += consumed;
            state->receive_credit_notification.notify();
         }
         co_return out;
      }
      (void)co_await state->read_notification.async_wait(observed);
   }
}

boost::asio::awaitable<void> session::impl::close_stream(const std::shared_ptr<stream_state>& state) {
   co_await ensure_started();
   co_await write_prepared([this, state]() -> std::optional<detail::bytes> {
      auto lock = std::scoped_lock{mutex_};
      require_stream_owned_locked(state);
      if (state->local_fin || state->reset) {
         return std::nullopt;
      }
      auto encoded = detail::encode_frame(detail::frame_type::data, detail::fin, state->id, 0);
      state->local_fin = true;
      return encoded;
   });
}

bool session::impl::is_reclaimable_stream_locked(const stream_state& state) const noexcept {
   if (state.reset) {
      return true;
   }
   return state.local_fin && state.remote_fin && state.inbound.empty();
}

void session::impl::reclaim_closed_streams_locked() {
   for (auto it = streams_.begin(); it != streams_.end();) {
      auto& state = *it->second;
      if (!is_reclaimable_stream_locked(state)) {
         ++it;
         continue;
      }
      std::erase(pending_accepts_, it->first);
      release_stream_buffers_locked(state);
      notify_stream_waiters_locked(it->second);
      it = streams_.erase(it);
   }
}

void session::impl::reset_stream(std::uint32_t id) {
   auto lock = std::scoped_lock{mutex_};
   const auto found = streams_.find(id);
   if (found == streams_.end()) {
      return;
   }
   reset_stream_locked(found->second);
}

void session::impl::cancel_stream(const std::shared_ptr<stream_state>& state) {
   {
      auto lock = std::scoped_lock{mutex_};
      reset_stream_locked(state);
   }
   auto executor = std::optional<boost::asio::any_io_executor>{};
   {
      auto lock = std::scoped_lock{mutex_};
      executor = executor_;
   }
   if (!executor) {
      return;
   }

   auto self = shared_from_this();
   boost::asio::co_spawn(
       *executor, self->write_frame(detail::frame_type::data, detail::rst, state->id, 0, {}, true),
       [self](std::exception_ptr) { (void)self; });
}

void session::impl::reset_stream_locked(const std::shared_ptr<stream_state>& state) {
   state->reset = true;
   release_stream_buffers_locked(*state);
   notify_stream_waiters_locked(state);
}

void session::impl::release_stream_buffers_locked(stream_state& state) {
   if (state.buffered == 0) {
      return;
   }
   session_buffer_ = state.buffered > session_buffer_ ? 0 : session_buffer_ - state.buffered;
   state.buffered = 0;
   state.inbound.clear();
}

void session::impl::notify_stream_waiters_locked(const std::shared_ptr<stream_state>& state) {
   state->read_notification.notify();
   state->window_notification.notify();
   state->receive_credit_notification.notify();
}

} // namespace forge::net::yamux

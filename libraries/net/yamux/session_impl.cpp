module;

#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.yamux.session;

import forge.asio.gate;
import forge.asio.notification;
import forge.net.transport.exceptions;

#include "details/session_impl.hxx"
#include "details/session_impl_stream_state.hxx"
#include "details/session_impl_stream_model.hxx"

namespace forge::net::yamux {
namespace {

[[nodiscard]] std::exception_ptr make_exception(exceptions::code value, std::string message) {
   try {
      switch (value) {
      case exceptions::code::invalid_options:
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, message);
      case exceptions::code::protocol_error:
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, message);
      case exceptions::code::resource_limit:
         FORGE_THROW_EXCEPTION(exceptions::resource_limit, message);
      case exceptions::code::stream_reset:
         FORGE_THROW_EXCEPTION(exceptions::stream_reset, message);
      case exceptions::code::closed:
         FORGE_THROW_EXCEPTION(exceptions::closed, message);
      case exceptions::code::canceled:
         FORGE_THROW_EXCEPTION(exceptions::canceled, message);
      }
   } catch (...) {
      return std::current_exception();
   }
   return {};
}

void cancel_timer_noexcept(boost::asio::steady_timer& timer) noexcept {
   try {
      timer.cancel();
   } catch (...) {
   }
}

} // namespace

session::impl::impl(transport::stream stream, side session_side, options session_options)
    : stream_(std::move(stream)), side_(session_side), options_(session_options) {
   validate_options();
   next_stream_id_ = side_ == side::initiator ? 1U : 2U;
}

bool session::impl::valid() const noexcept {
   auto lock = std::scoped_lock{mutex_};
   return stream_.valid() && !closed_ && !canceled_;
}

bool session::impl::exceeds_limit(std::size_t current, std::size_t addition, std::size_t limit) noexcept {
   return current > limit || addition > limit - current;
}

bool session::impl::remote_opens_stream(side local_side, std::uint32_t stream_id) noexcept {
   const auto remote_is_initiator = local_side == side::responder;
   const auto id_is_odd = (stream_id % 2U) == 1U;
   return remote_is_initiator == id_is_odd;
}

void session::impl::validate_options() const {
   if (!stream_.valid()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "yamux requires a valid transport stream");
   }
   if (options_.initial_window < detail::initial_stream_window ||
       options_.max_stream_window < options_.initial_window || options_.max_frame_size == 0 ||
       options_.max_streams == 0 || options_.max_pending_accepts == 0 ||
       options_.max_stream_buffer < options_.initial_window ||
       options_.max_session_buffer < options_.initial_window || options_.close_timeout.count() <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid yamux options");
   }
   if (options_.max_frame_size > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "yamux frame size exceeds wire limit");
   }
}

std::uint32_t session::impl::local_window_delta() const noexcept {
   return options_.initial_window - detail::initial_stream_window;
}

std::uint32_t session::impl::checked_peer_window(std::uint32_t current, std::uint32_t delta) const {
   if (current > options_.max_stream_window || delta > (std::numeric_limits<std::uint32_t>::max)() - current ||
       delta > options_.max_stream_window - current) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux peer window update exceeds configured limit");
   }
   return current + delta;
}

boost::asio::awaitable<void> session::impl::ensure_started() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto start = false;
   {
      auto lock = std::scoped_lock{mutex_};
      if (!executor_) {
         executor_ = executor;
      }
      if (!started_) {
         started_ = true;
         start = true;
      }
   }
   if (start) {
      auto self = shared_from_this();
      boost::asio::co_spawn(executor, self->read_loop(), [self](std::exception_ptr error) {
         if (error) {
            self->fail_session(exceptions::code::closed, "yamux read loop stopped");
            self->finish_read_loop();
         }
      });
   }
}

boost::asio::awaitable<void> session::impl::async_close() {
   co_await ensure_started();
   if (!start_close()) {
      co_await wait_for_close();
      co_return;
   }
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});

   auto error = std::exception_ptr{};
   auto deadline_timer = std::shared_ptr<boost::asio::steady_timer>{};
   try {
      auto executor = co_await boost::asio::this_coro::executor;
      const auto deadline = std::chrono::steady_clock::now() + options_.close_timeout;
      deadline_timer = std::make_shared<boost::asio::steady_timer>(executor, deadline);
      auto weak = weak_from_this();
      deadline_timer->async_wait([weak, deadline_timer](const boost::system::error_code& timer_error) {
         if (timer_error) {
            return;
         }
         if (auto self = weak.lock()) {
            self->fail_session(exceptions::code::closed, "yamux close deadline expired");
            self->stream_.cancel();
         }
      });

      auto terminal_writer = forge::asio::gate::ticket{};
      try {
         terminal_writer = co_await write_gate_.acquire();
      } catch (const forge::asio::exceptions::rejected&) {
         // A prior terminal transition closed the gate after all admitted writes drained.
      }

      if (terminal_writer.active()) {
         try {
            auto outbound = transport::chunk{
                detail::encode_frame(detail::frame_type::go_away, 0, 0, detail::go_away_normal)};
            co_await stream_.async_write(std::move(outbound));
         } catch (...) {
            // Closing is best-effort once the underlying byte stream has already failed.
         }
      }
      try {
         co_await stream_.async_close();
      } catch (...) {
      }
      fail_session(exceptions::code::closed, "yamux session closed");
      if (!co_await wait_for_read_loop_until(deadline)) {
         stream_.cancel();
         co_await wait_for_read_loop();
      }
   } catch (...) {
      error = std::current_exception();
      try {
         stream_.cancel();
      } catch (...) {
      }
   }
   if (deadline_timer) {
      cancel_timer_noexcept(*deadline_timer);
   }
   finish_close(error);
   if (error) {
      std::rethrow_exception(error);
   }
}

void session::impl::cancel() {
   fail_session(exceptions::code::canceled, "yamux session canceled");
   stream_.cancel();
}

std::shared_ptr<session::impl::stream_state> session::impl::make_stream_locked(std::uint32_t id,
                                                                              std::uint32_t send_window) {
   if (send_window > options_.max_stream_window) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux peer window exceeds configured limit");
   }
   auto state = std::make_shared<stream_state>(id);
   state->send_window = send_window;
   state->receive_window = options_.initial_window;
   return state;
}

void session::impl::require_stream_owned_locked(const std::shared_ptr<stream_state>& state) const {
   if (state->reset) {
      FORGE_THROW_EXCEPTION(exceptions::stream_reset, "yamux stream reset");
   }
   const auto found = streams_.find(state->id);
   if (found == streams_.end() || found->second != state) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "yamux stream does not exist");
   }
}

bool session::impl::stream_valid(const std::shared_ptr<stream_state>& state) const noexcept {
   auto lock = std::scoped_lock{mutex_};
   const auto found = streams_.find(state->id);
   return found != streams_.end() && found->second == state && !closed_ && !canceled_ && !state->reset;
}

transport::stream session::impl::make_transport_stream(const std::shared_ptr<stream_state>& state) {
   return transport::detail::stream_access::make(
       std::make_shared<stream_model>(shared_from_this(), state));
}

void session::impl::rethrow_terminal_locked() const {
   if (terminal_error_) {
      std::rethrow_exception(terminal_error_);
   }
   if (canceled_) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "yamux session canceled");
   }
   if (closed_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "yamux session closed");
   }
}

bool session::impl::start_close() {
   auto lock = std::scoped_lock{mutex_};
   if (close_started_) {
      return false;
   }
   close_started_ = true;
   closed_ = true;
   wake_all_locked();
   return true;
}

boost::asio::awaitable<void> session::impl::wait_for_close() {
   auto error = std::exception_ptr{};
   while (true) {
      const auto observed = close_notification_.epoch();
      {
         auto lock = std::scoped_lock{mutex_};
         if (close_done_) {
            error = close_error_;
            break;
         }
      }
      (void)co_await close_notification_.async_wait(observed);
   }
   if (error) {
      std::rethrow_exception(error);
   }
}

void session::impl::finish_close(std::exception_ptr error) noexcept {
   {
      auto lock = std::scoped_lock{mutex_};
      close_error_ = std::move(error);
      close_done_ = true;
   }
   close_notification_.notify();
}

void session::impl::fail_session(exceptions::code value, std::string message) {
   auto first_transition = false;
   {
      auto lock = std::scoped_lock{mutex_};
      if (terminal_error_) {
         return;
      }
      terminal_error_ = make_exception(value, std::move(message));
      first_transition = true;
      if (value == exceptions::code::canceled) {
         canceled_ = true;
      } else {
         closed_ = true;
      }
      for (const auto& [_, state] : streams_) {
         state->reset = value == exceptions::code::protocol_error || value == exceptions::code::resource_limit;
      }
      wake_all_locked();
   }
   if (first_transition) {
      write_gate_.close();
   }
}

void session::impl::wake_all_locked() {
   accept_notification_.notify();
   for (const auto& [_, state] : streams_) {
      state->read_notification.notify();
      state->window_notification.notify();
      state->receive_credit_notification.notify();
   }
}

boost::asio::awaitable<bool>
session::impl::write_prepared(std::function<std::optional<detail::bytes>()> prepare, bool allow_after_close,
                              std::shared_ptr<void> lifetime) {
   if (!allow_after_close) {
      auto lock = std::scoped_lock{mutex_};
      rethrow_terminal_locked();
   }

   auto ticket = forge::asio::gate::ticket{};
   try {
      ticket = co_await write_gate_.acquire();
   } catch (const forge::asio::exceptions::canceled&) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "yamux write was canceled while waiting");
   } catch (const forge::asio::exceptions::rejected&) {
      auto lock = std::scoped_lock{mutex_};
      rethrow_terminal_locked();
      FORGE_THROW_EXCEPTION(exceptions::closed, "yamux write gate is closed");
   }

   // A prepared state transition and its serialized frame complete together once this owns the writer.
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});

   if (!allow_after_close) {
      auto lock = std::scoped_lock{mutex_};
      rethrow_terminal_locked();
   }

   auto encoded = prepare();
   if (!encoded) {
      co_return false;
   }

   try {
      auto outbound = transport::chunk{std::move(*encoded)};
      transport::detail::chunk_access::attach_lifetime(outbound, std::move(lifetime));
      co_await stream_.async_write(std::move(outbound));
   } catch (...) {
      fail_session(exceptions::code::closed, "yamux underlying stream write failed");
      FORGE_THROW_EXCEPTION(exceptions::closed, "yamux underlying stream write failed");
   }
   co_return true;
}

boost::asio::awaitable<void> session::impl::write_frame(detail::frame_type type, std::uint16_t flags,
                                                        std::uint32_t stream_id, std::uint32_t length,
                                                        std::span<const std::uint8_t> payload,
                                                        bool allow_after_close,
                                                        std::shared_ptr<void> lifetime) {
   (void)co_await write_prepared(
       [type, flags, stream_id, length, payload]() -> std::optional<detail::bytes> {
          return detail::encode_frame(type, flags, stream_id, length, payload);
       },
       allow_after_close, std::move(lifetime));
}

boost::asio::awaitable<void> session::impl::wait_for_read_loop() {
   while (true) {
      const auto observed = read_loop_notification_.epoch();
      {
         auto lock = std::scoped_lock{mutex_};
         if (read_loop_done_) {
            co_return;
         }
      }
      (void)co_await read_loop_notification_.async_wait(observed);
   }
}

boost::asio::awaitable<bool>
session::impl::wait_for_read_loop_until(std::chrono::steady_clock::time_point deadline) {
   while (true) {
      const auto observed = read_loop_notification_.epoch();
      {
         auto lock = std::scoped_lock{mutex_};
         if (read_loop_done_) {
            co_return true;
         }
      }
      (void)co_await read_loop_notification_.async_wait_until(observed, deadline);
      {
         auto lock = std::scoped_lock{mutex_};
         if (read_loop_done_) {
            co_return true;
         }
      }
      if (std::chrono::steady_clock::now() >= deadline) {
         co_return false;
      }
   }
}

void session::impl::finish_read_loop() {
   {
      auto lock = std::scoped_lock{mutex_};
      read_loop_done_ = true;
   }
   read_loop_notification_.notify();
}

} // namespace forge::net::yamux

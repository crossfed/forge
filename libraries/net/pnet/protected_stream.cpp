module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/this_coro.hpp>

module forge.net.pnet.protector;

import forge.asio.gate;
import forge.asio.notification;
import forge.crypto.symmetric.xsalsa20;
import forge.net.transport.stream;

#include "details/protected_stream.hxx"

namespace forge::net::pnet::detail {

forge::net::transport::stream_connection protected_stream::wrap(
    forge::net::transport::stream_connection connection, std::shared_ptr<const pre_shared_key> key,
    std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size> local_nonce,
    boost::asio::any_io_executor executor) {
   if (!key || pre_shared_key_access::bytes(*key).size() != pre_shared_key_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "pnet transport requires a 32-byte pre-shared key");
   }

   auto model = std::shared_ptr<protected_stream>{
       new protected_stream{std::move(connection.stream), std::move(key), local_nonce, std::move(executor)}};
   auto weak = std::weak_ptr<protected_stream>{model};
   connection.stream = forge::net::transport::detail::stream_access::make_cancelable(
       std::move(model),
       [weak]() noexcept {
          if (auto value = weak.lock()) {
             value->request_cancel();
          }
       },
       [weak]() noexcept {
          if (auto value = weak.lock()) {
             value->request_cancel();
          }
       });
   return connection;
}

protected_stream::protected_stream(
    forge::net::transport::stream stream, std::shared_ptr<const pre_shared_key> key,
    const std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size>& local_nonce,
    boost::asio::any_io_executor executor)
    : lower_stream_{std::make_shared<forge::net::transport::stream>(std::move(stream))}, key_{std::move(key)},
      executor_{std::move(executor)}, write_cipher_{make_cipher(local_nonce)} {
   pending_peer_nonce_.reserve(forge::crypto::symmetric::xsalsa20::nonce_size);
}

protected_stream::~protected_stream() = default;

bool protected_stream::valid() const noexcept {
   if (terminal_.load(std::memory_order_acquire) != terminal_state::active) {
      return false;
   }
   if (const auto lower = lower_stream()) {
      return lower->valid();
   }
   return false;
}

std::int64_t protected_stream::id() const noexcept {
   if (const auto lower = lower_stream()) {
      return lower->id();
   }
   return -1;
}

void protected_stream::require_active() const {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "pnet stream is closed");
   }
}

void protected_stream::begin_close() noexcept {
   auto expected = terminal_state::active;
   static_cast<void>(terminal_.compare_exchange_strong(expected, terminal_state::closing, std::memory_order_acq_rel,
                                                        std::memory_order_acquire));
   request_lower_cancel();
   start_terminal_worker();
}

void protected_stream::request_cancel() noexcept {
   auto current = terminal_.load(std::memory_order_acquire);
   auto claimed = false;
   while (current == terminal_state::active || current == terminal_state::closing) {
      if (terminal_.compare_exchange_weak(current, terminal_state::cancel_requested, std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
         claimed = true;
         break;
      }
   }
   if (claimed) {
      request_lower_cancel();
   }
   start_terminal_worker();
}

void protected_stream::request_lower_cancel() noexcept {
   if (lower_cancel_requested_.exchange(true, std::memory_order_acq_rel)) {
      return;
   }
   if (const auto lower = lower_stream()) {
      lower->request_cancel();
   }
}

void protected_stream::start_terminal_worker() noexcept {
   if (terminal_worker_started_.exchange(true, std::memory_order_acq_rel)) {
      return;
   }
   try {
      auto self = shared_from_this();
      auto completion_owner = self;
      auto lower = lower_stream();
      boost::asio::co_spawn(
          executor_,
          [self = std::move(self), lower = std::move(lower)]() mutable -> boost::asio::awaitable<void> {
             co_await self->async_terminal_worker(std::move(lower));
          },
          [self = std::move(completion_owner)](std::exception_ptr error) noexcept {
             if (error) {
                self->finish_terminal(std::move(error));
             }
          });
   } catch (...) {
      finish_terminal(std::current_exception());
   }
}

boost::asio::awaitable<void>
protected_stream::async_terminal_worker(std::shared_ptr<forge::net::transport::stream> lower) {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});

   auto error = std::exception_ptr{};
   try {
      auto write_ticket = co_await write_gate_.acquire();
      auto read_ticket = co_await read_gate_.acquire();
      if (lower) {
         co_await lower->async_close();
      }
   } catch (...) {
      error = std::current_exception();
   }

   lower.reset();
   finish_terminal(std::move(error));
}

boost::asio::awaitable<void> protected_stream::wait_for_terminal() {
   auto error = std::exception_ptr{};
   while (true) {
      const auto observed = terminal_notification_.epoch();
      {
         const auto lock = std::scoped_lock{terminal_mutex_};
         if (terminal_done_) {
            error = terminal_error_;
            break;
         }
      }
      static_cast<void>(co_await terminal_notification_.async_wait(observed));
   }
   if (error) {
      std::rethrow_exception(error);
   }
}

void protected_stream::finish_terminal(std::exception_ptr error) noexcept {
   {
      const auto lock = std::scoped_lock{lower_mutex_};
      lower_stream_.reset();
   }
   {
      const auto lock = std::scoped_lock{terminal_mutex_};
      if (terminal_done_) {
         return;
      }
      terminal_error_ = std::move(error);
      terminal_done_ = true;
      terminal_.store(terminal_state::completed, std::memory_order_release);
   }
   terminal_notification_.notify();
}

std::shared_ptr<forge::net::transport::stream> protected_stream::lower_stream() const {
   const auto lock = std::scoped_lock{lower_mutex_};
   return lower_stream_;
}

std::unique_ptr<forge::crypto::symmetric::xsalsa20::stream>
protected_stream::make_cipher(
    const std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size>& nonce) const {
   auto key = forge::crypto::symmetric::xsalsa20::key{pre_shared_key_access::bytes(*key_)};
   return std::make_unique<forge::crypto::symmetric::xsalsa20::stream>(
       key, forge::crypto::symmetric::xsalsa20::nonce{.bytes = nonce});
}

boost::asio::awaitable<void> protected_stream::async_write(std::span<const std::uint8_t> bytes) {
   auto ticket = forge::asio::gate::ticket{};
   try {
      require_active();
      ticket = co_await write_gate_.acquire();
      require_active();
      auto lower = lower_stream();
      if (!lower) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "pnet stream has no lower transport");
      }

      auto payload = std::vector<std::uint8_t>{bytes.begin(), bytes.end()};
      write_cipher_->transform(payload);
      co_await lower->async_write(std::span<const std::uint8_t>{payload});
   } catch (...) {
      ticket.release();
      request_cancel();
      throw;
   }
}

boost::asio::awaitable<std::vector<std::uint8_t>> protected_stream::async_read() {
   auto ticket = forge::asio::gate::ticket{};
   try {
      require_active();
      ticket = co_await read_gate_.acquire();
      require_active();
      auto lower = lower_stream();
      if (!lower) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "pnet stream has no lower transport");
      }

      while (!read_cipher_) {
         auto incoming = co_await lower->async_read();
         if (incoming.empty()) {
            if (pending_peer_nonce_.empty()) {
               FORGE_THROW_EXCEPTION(exceptions::closed, "pnet peer closed before its nonce");
            }
            FORGE_THROW_EXCEPTION(exceptions::closed, "pnet peer closed with a truncated nonce");
         }

         const auto needed = forge::crypto::symmetric::xsalsa20::nonce_size - pending_peer_nonce_.size();
         const auto nonce_bytes = std::min(needed, incoming.size());
         pending_peer_nonce_.insert(pending_peer_nonce_.end(), incoming.begin(), incoming.begin() + nonce_bytes);
         if (pending_peer_nonce_.size() != forge::crypto::symmetric::xsalsa20::nonce_size) {
            continue;
         }

         auto nonce = std::array<std::uint8_t, forge::crypto::symmetric::xsalsa20::nonce_size>{};
         std::copy(pending_peer_nonce_.begin(), pending_peer_nonce_.end(), nonce.begin());
         pending_peer_nonce_.clear();
         read_cipher_ = make_cipher(nonce);

         auto payload = std::vector<std::uint8_t>{incoming.begin() + nonce_bytes, incoming.end()};
         if (!payload.empty()) {
            read_cipher_->transform(payload);
            co_return payload;
         }
      }

      while (true) {
         auto payload = co_await lower->async_read();
         if (payload.empty()) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "pnet peer closed after its nonce");
         }
         read_cipher_->transform(payload);
         co_return payload;
      }
   } catch (...) {
      ticket.release();
      request_cancel();
      throw;
   }
}

boost::asio::awaitable<void> protected_stream::async_close() {
   begin_close();
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   co_await wait_for_terminal();
}

void protected_stream::cancel() {
   request_cancel();
}

} // namespace forge::net::pnet::detail

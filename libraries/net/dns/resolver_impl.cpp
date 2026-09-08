module;

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <forge/exceptions/macros.hpp>

#include <ares.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.dns.resolver;

import forge.asio.notification;

#include "details/resolver_impl.hxx"
#include "details/resolver_query.hxx"

namespace forge::net::dns {
namespace {

namespace asio = boost::asio;

inline constexpr auto maximum_in_flight = std::size_t{1024};

[[noreturn]] void throw_invalid_options(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, message);
}

[[noreturn]] void throw_resource_limit(std::string_view limit, std::size_t maximum, std::size_t actual) {
   FORGE_THROW_EXCEPTION(exceptions::resource_limit, "dns response exceeded configured resource limit",
                         forge::exceptions::ctx("limit", limit), forge::exceptions::ctx("maximum", maximum),
                         forge::exceptions::ctx("actual", actual));
}

void validate_resolver_options(const resolver_options& options) {
   if (options.max_in_flight == 0 || options.max_in_flight > maximum_in_flight) {
      throw_invalid_options("dns max_in_flight must be between 1 and 1024");
   }
   for (const auto& server : options.nameservers) {
      auto error = boost::system::error_code{};
      const auto address = asio::ip::make_address(server.address, error);
      const auto valid_scope_id = std::all_of(server.scope_id.begin(), server.scope_id.end(), [](char value) {
         const auto byte = static_cast<unsigned char>(value);
         return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                byte == '.' || byte == '_' || byte == '-';
      });
      if (error || server.port == 0 || !valid_scope_id || server.scope_id.find('%') != std::string::npos) {
         throw_invalid_options("dns nameservers must use a numeric address and non-zero port");
      }
      if ((!address.is_v6() || !address.to_v6().is_link_local()) && !server.scope_id.empty()) {
         throw_invalid_options("dns nameserver scope_id is only valid for IPv6 link-local addresses");
      }
      if (address.is_v6() && address.to_v6().is_link_local() && server.scope_id.empty()) {
         throw_invalid_options("dns IPv6 link-local nameservers require a scope_id");
      }
   }
}

} // namespace

resolver::impl::impl(boost::asio::any_io_executor executor, resolver_options options_value)
    : strand(asio::make_strand(std::move(executor))), options(std::move(options_value)),
      close_changed(std::make_shared<forge::asio::notification>()) {
   validate_resolver_options(options);
}

resolver::impl::~impl() = default;

boost::asio::awaitable<address_response>
resolver::impl::async_resolve_addresses(std::shared_ptr<impl> state, std::string name, address_family family,
                                        query_options options, std::stop_token stop) {
   auto executor = state->strand;
   co_return co_await asio::co_spawn(
       std::move(executor),
       [state = std::move(state), name = std::move(name), family, options = std::move(options), stop]() mutable
           -> boost::asio::awaitable<address_response> {
          if (state->closing) {
             FORGE_THROW_EXCEPTION(exceptions::closed, "dns resolver is closed");
          }
          if (state->operations.size() >= state->options.max_in_flight) {
             throw_resource_limit("max_in_flight", state->options.max_in_flight, state->operations.size() + 1);
          }

          const auto id = state->next_operation_id++;
          auto operation = resolver_query::make_address_query(state, id, std::move(name), family, std::move(options));
          state->operations.emplace(id, operation);
          state->active_count.store(state->operations.size(), std::memory_order_release);
          auto cancellation = std::stop_callback{stop, [weak = std::weak_ptr<resolver_query>{operation}] {
                                                     if (const auto current = weak.lock()) {
                                                        current->request_cancel_from_any_thread();
                                                     }
                                                  }};
          try {
             if (stop.stop_requested()) {
                operation->request_cancel_on_strand();
             } else {
                operation->start();
             }
          } catch (...) {
             operation->abandon_start();
             state->operations.erase(id);
             state->active_count.store(state->operations.size(), std::memory_order_release);
             throw;
          }
          try {
             co_return co_await operation->async_wait_addresses();
          } catch (...) {
             operation->request_cancel_from_any_thread();
             throw;
          }
       },
       asio::use_awaitable);
}

boost::asio::awaitable<text_response>
resolver::impl::async_resolve_txt(std::shared_ptr<impl> state, std::string name, query_options options,
                                  std::stop_token stop) {
   auto executor = state->strand;
   co_return co_await asio::co_spawn(
       std::move(executor),
       [state = std::move(state), name = std::move(name), options = std::move(options), stop]() mutable
           -> boost::asio::awaitable<text_response> {
          if (state->closing) {
             FORGE_THROW_EXCEPTION(exceptions::closed, "dns resolver is closed");
          }
          if (state->operations.size() >= state->options.max_in_flight) {
             throw_resource_limit("max_in_flight", state->options.max_in_flight, state->operations.size() + 1);
          }

          const auto id = state->next_operation_id++;
          auto operation = resolver_query::make_text_query(state, id, std::move(name), std::move(options));
          state->operations.emplace(id, operation);
          state->active_count.store(state->operations.size(), std::memory_order_release);
          auto cancellation = std::stop_callback{stop, [weak = std::weak_ptr<resolver_query>{operation}] {
                                                     if (const auto current = weak.lock()) {
                                                        current->request_cancel_from_any_thread();
                                                     }
                                                  }};
          try {
             if (stop.stop_requested()) {
                operation->request_cancel_on_strand();
             } else {
                operation->start();
             }
          } catch (...) {
             operation->abandon_start();
             state->operations.erase(id);
             state->active_count.store(state->operations.size(), std::memory_order_release);
             throw;
          }
          try {
             co_return co_await operation->async_wait_text();
          } catch (...) {
             operation->request_cancel_from_any_thread();
             throw;
          }
       },
       asio::use_awaitable);
}

boost::asio::awaitable<void> resolver::impl::async_close(std::shared_ptr<impl> state) {
   co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});
   if (!state) {
      co_return;
   }
   auto executor = state->strand;
   co_await asio::co_spawn(
       std::move(executor),
       [state = std::move(state)]() -> boost::asio::awaitable<void> {
          co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});
          state->close_on_strand();
          while (!state->operations.empty()) {
             const auto observed = state->close_changed->epoch();
             if (!state->operations.empty()) {
                static_cast<void>(co_await state->close_changed->async_wait(observed));
             }
          }
       },
       asio::use_awaitable);
}

std::size_t resolver::impl::active_queries() const noexcept {
   return active_count.load(std::memory_order_acquire);
}

void resolver::impl::request_cancel() noexcept {
   auto self = shared_from_this();
   try {
      asio::post(strand, [self = std::move(self)] { self->cancel_all(terminal_reason::canceled); });
   } catch (...) {
   }
}

void resolver::impl::request_close() noexcept {
   auto self = shared_from_this();
   try {
      asio::post(strand, [self = std::move(self)] { self->close_on_strand(); });
   } catch (...) {
   }
}

void resolver::impl::cancel_all(terminal_reason reason) noexcept {
   for (const auto& [_, operation] : operations) {
      if (reason == terminal_reason::closed) {
         operation->request_close_on_strand();
      } else {
         operation->request_cancel_on_strand();
      }
   }
}

void resolver::impl::close_on_strand() noexcept {
   if (!closing) {
      closing = true;
      cancel_all(terminal_reason::closed);
   }
   close_changed->notify();
}

void resolver::impl::retire(std::uint64_t id, const std::shared_ptr<resolver_query>& operation) noexcept {
   const auto iterator = operations.find(id);
   if (iterator == operations.end() || iterator->second != operation) {
      return;
   }
   operations.erase(iterator);
   active_count.store(operations.size(), std::memory_order_release);
   operation->release_owner();
   close_changed->notify();
}

} // namespace forge::net::dns

module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.api.transport.server;

import forge.api.stream.server;
import forge.net.transport.exceptions;

namespace forge::api::transport {
namespace {

[[nodiscard]] bool is_clean_close(const forge::exceptions::base& error) noexcept {
   return forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::closed) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::canceled);
}

} // namespace

boost::asio::awaitable<void> serve_session(forge::net::transport::session session, forge::api::core::binding_plan plan,
                                           session_options value) {
   if (value.max_concurrent_streams == 0) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "API transport max concurrent streams must be positive");
   }

   struct state {
      explicit state(boost::asio::any_io_executor executor)
          : strand(boost::asio::make_strand(std::move(executor))), wake(strand) {
         wake.expires_at(boost::asio::steady_timer::time_point::max());
      }

      boost::asio::strand<boost::asio::any_io_executor> strand;
      boost::asio::steady_timer wake;
      std::size_t slots = 0;
   };

   const auto executor = co_await boost::asio::this_coro::executor;
   auto shared = std::make_shared<state>(executor);
   auto reserve_slot = [shared, max = value.max_concurrent_streams]() -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(shared->strand, boost::asio::use_awaitable);
      while (shared->slots >= max) {
         shared->wake.expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await shared->wake.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
      ++shared->slots;
   };
   auto release_slot = [shared]() -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(shared->strand, boost::asio::use_awaitable);
      if (shared->slots > 0) {
         --shared->slots;
      }
      shared->wake.cancel();
   };
   auto wait_for_drain = [shared]() -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(shared->strand, boost::asio::use_awaitable);
      while (shared->slots > 0) {
         shared->wake.expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await shared->wake.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
   };

   auto accepting = true;
   while (accepting) {
      auto reserved = false;
      auto release_reserved = false;
      auto pending_error = std::exception_ptr{};
      try {
         co_await reserve_slot();
         reserved = true;
         auto stream = co_await session.async_accept_stream();
         boost::asio::co_spawn(
             executor,
             [release_slot, stream = std::move(stream), plan, stream_options = value.stream]() mutable
             -> boost::asio::awaitable<void> {
                try {
                   co_await forge::api::stream::serve_stream(std::move(stream), std::move(plan), stream_options);
                } catch (const forge::exceptions::base&) {
                   // A bad API stream closes that stream; the session accept loop owns admission.
                } catch (...) {
                   // Detached stream failures must still release their reserved admission slot.
                }
                co_await release_slot();
             },
             boost::asio::detached);
      } catch (const forge::exceptions::base& error) {
         if (reserved) {
            release_reserved = true;
         }
         if (is_clean_close(error)) {
            accepting = false;
         } else {
            pending_error = std::current_exception();
         }
      } catch (...) {
         if (reserved) {
            release_reserved = true;
         }
         pending_error = std::current_exception();
      }
      if (release_reserved) {
         co_await release_slot();
      }
      if (pending_error) {
         std::rethrow_exception(pending_error);
      }
   }
   co_await wait_for_drain();
}

} // namespace forge::api::transport

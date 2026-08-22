module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/compat/move_only_function.hpp>

#include <functional>
#include <memory>
#include <utility>

module forge.net.p2p.node;

import forge.net.p2p.stream;

#include "details/owner_cancellation.hxx"

namespace forge::net::p2p::detail {

owner_stream_cancellation::owner_stream_cancellation(
    boost::asio::cancellation_slot slot,
    std::shared_ptr<forge::net::p2p::stream> stream)
    : slot_(std::move(slot)), stream_(std::move(stream)) {
   auto weak = std::weak_ptr<forge::net::p2p::stream>{stream_};
   if (slot_.is_connected()) {
      slot_.assign([weak = std::move(weak)](boost::asio::cancellation_type) noexcept {
         if (auto stream = weak.lock()) {
            stream->request_cancel();
         }
      });
   }
}

owner_stream_cancellation::~owner_stream_cancellation() noexcept {
   try {
      slot_.clear();
   } catch (...) {
      // A custom cancellation slot cannot escape operation cleanup.
   }
}

void owner_stream_cancellation::request_cancel() noexcept {
   if (stream_) {
      stream_->request_cancel();
   }
}

boost::asio::awaitable<void>
async_run_with_owner_cancellation(std::shared_ptr<worker_stop_bridge> stop,
                                  owner_cancellable_work work,
                                  worker_stop_bridge_options options) {
   namespace asio = boost::asio;

   co_await async_run_with_stop_bridge(
       std::move(stop),
       [work = std::move(work)](std::shared_ptr<worker_terminal_owner> terminal) mutable
           -> asio::awaitable<void> {
          const auto executor = co_await asio::this_coro::executor;
          auto task_signal = std::make_shared<asio::cancellation_signal>();
          auto stream_signal = std::make_shared<asio::cancellation_signal>();
          co_await asio::co_spawn(
              executor,
              [terminal = std::move(terminal), task_signal, stream_signal,
               work = std::move(work)]() mutable
                  -> asio::awaitable<void> {
                 static_cast<void>(terminal->publish(worker_terminal_owner::callback{
                     [task_signal, stream_signal]() noexcept {
                        try {
                           stream_signal->emit(asio::cancellation_type::all);
                        } catch (...) {
                           // A custom cancellation handler cannot escape the
                           // terminal boundary or corrupt sibling operations.
                        }
                        try {
                           task_signal->emit(asio::cancellation_type::all);
                        } catch (...) {
                           // Coroutine cancellation remains independent from
                           // the published stream's terminal handler.
                        }
                     },
                 }));
                 co_await work(stream_signal->slot());
              },
              asio::bind_cancellation_slot(task_signal->slot(), asio::use_awaitable));
       },
       std::move(options));
}

} // namespace forge::net::p2p::detail

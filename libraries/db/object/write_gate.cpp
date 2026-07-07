module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

module forge.db.object.store;

#include "details/write_gate.hxx"

namespace forge::db::object::detail {

enum class wait_state : std::uint8_t {
   queued,
   granted,
   cancelled,
   completed,
};

struct write_gate::waiter {
   explicit waiter(boost::asio::any_io_executor executor)
       : strand{boost::asio::make_strand(std::move(executor))},
         timer{strand, boost::asio::steady_timer::time_point::max()} {}

   boost::asio::strand<boost::asio::any_io_executor> strand;
   boost::asio::steady_timer timer;
   wait_state state = wait_state::queued;
};

write_gate::ticket::ticket(std::shared_ptr<write_gate> gate) : gate_{std::move(gate)} {}

write_gate::ticket::ticket(ticket&& other) noexcept : gate_{std::move(other.gate_)} {}

write_gate::ticket& write_gate::ticket::operator=(ticket&& other) noexcept {
   if (this != &other) {
      release();
      gate_ = std::move(other.gate_);
   }
   return *this;
}

write_gate::ticket::~ticket() {
   release();
}

void write_gate::ticket::release() noexcept {
   auto gate = std::move(gate_);
   if (gate) {
      gate->release_one();
   }
}

boost::asio::awaitable<write_gate::ticket> write_gate::acquire() {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto cancellation = co_await boost::asio::this_coro::cancellation_state;
   const auto self = shared_from_this();

   for (;;) {
      auto waiter = std::shared_ptr<write_gate::waiter>{};
      {
         auto lock = std::scoped_lock{mutex_};
         if (!held_) {
            held_ = true;
            co_return ticket{self};
         }
         waiter = std::make_shared<write_gate::waiter>(executor);
         waiters_.push_back(waiter);
      }

      auto slot = cancellation.slot();
      if (slot.is_connected()) {
         slot.assign([self, waiter](boost::asio::cancellation_type_t type) {
            if (type != boost::asio::cancellation_type::none) {
               self->cancel(waiter);
            }
         });
      }
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
         cancel(waiter);
         if (slot.is_connected()) {
            slot.clear();
         }
         throw boost::system::system_error{boost::asio::error::operation_aborted};
      }

      auto switch_error = boost::system::error_code{};
      co_await boost::asio::dispatch(waiter->strand,
                                     boost::asio::redirect_error(boost::asio::use_awaitable, switch_error));
      if (switch_error) {
         cancel(waiter);
         if (slot.is_connected()) {
            slot.clear();
         }
         throw boost::system::system_error{switch_error};
      }
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
         cancel(waiter);
         if (slot.is_connected()) {
            slot.clear();
         }
         throw boost::system::system_error{boost::asio::error::operation_aborted};
      }

      auto error = boost::system::error_code{};
      co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));

      const auto cancelled_after_wait = cancellation.cancelled() != boost::asio::cancellation_type::none;
      if (slot.is_connected()) {
         slot.clear();
      }
      if (cancelled_after_wait) {
         cancel(waiter);
         throw boost::system::system_error{boost::asio::error::operation_aborted};
      }

      auto throw_error = boost::system::error_code{};
      {
         auto lock = std::scoped_lock{mutex_};
         if (waiter->state == wait_state::granted) {
            waiter->state = wait_state::completed;
            co_return ticket{self};
         }

         if (waiter->state == wait_state::cancelled) {
            throw_error = error ? error : boost::asio::error::operation_aborted;
         } else {
            waiter->state = wait_state::cancelled;
            const auto found = std::ranges::find(waiters_, waiter);
            if (found != waiters_.end()) {
               waiters_.erase(found);
            }
            throw_error = error ? error : boost::asio::error::operation_aborted;
         }
      }

      if (throw_error) {
         throw boost::system::system_error{throw_error};
      }
   }
}

void write_gate::cancel(std::shared_ptr<write_gate::waiter> waiter) noexcept {
   auto release_grant = false;
   {
      auto lock = std::scoped_lock{mutex_};
      if (waiter->state == wait_state::queued) {
         waiter->state = wait_state::cancelled;
         const auto found = std::ranges::find(waiters_, waiter);
         if (found != waiters_.end()) {
            waiters_.erase(found);
         }
      } else if (waiter->state == wait_state::granted) {
         waiter->state = wait_state::cancelled;
         release_grant = true;
      }
   }

   wake(waiter);
   if (release_grant) {
      release_one();
   }
}

void write_gate::release_one() noexcept {
   auto waiter = std::shared_ptr<write_gate::waiter>{};
   {
      auto lock = std::scoped_lock{mutex_};
      while (!waiters_.empty() && !waiter) {
         waiter = std::move(waiters_.front());
         waiters_.pop_front();
         if (waiter->state != wait_state::queued) {
            waiter.reset();
         }
      }
      if (waiter) {
         waiter->state = wait_state::granted;
      } else {
         held_ = false;
      }
   }
   if (waiter) {
      wake(waiter);
   }
}

void write_gate::wake(const std::shared_ptr<write_gate::waiter>& waiter) noexcept {
   boost::asio::dispatch(waiter->strand, [waiter] {
      try {
         waiter->timer.expires_at(boost::asio::steady_timer::time_point::min());
         waiter->timer.cancel();
      } catch (...) {
         // Wake-up is best-effort; gate cleanup paths must remain noexcept.
      }
   });
}

} // namespace forge::db::object::detail

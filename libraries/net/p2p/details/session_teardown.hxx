#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace forge::net::p2p::detail {

class session_teardown {
 private:
   struct state;

 public:
   struct operation {
      std::function<boost::asio::awaitable<void>()> close;
      std::function<void()> cancel;
   };

   class ticket {
    public:
      ticket() = default;
      ticket(const ticket&) = delete;
      ticket& operator=(const ticket&) = delete;
      ticket(ticket&& other) noexcept;
      ticket& operator=(ticket&& other) noexcept;
      ~ticket();

      [[nodiscard]] bool active() const noexcept;
      void release() noexcept;

    private:
      explicit ticket(std::shared_ptr<state> state, std::uint64_t id);

      std::shared_ptr<state> state_;
      std::uint64_t id_ = 0;
      friend class session_teardown;
   };

   explicit session_teardown(boost::asio::any_io_executor executor);

   [[nodiscard]] ticket track(std::function<void()> cancel = {}) noexcept;
   void start(std::vector<operation> operations) noexcept;
   boost::asio::awaitable<void> wait() const;

 private:
   std::shared_ptr<state> state_;
};

} // namespace forge::net::p2p::detail

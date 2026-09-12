#pragma once

#include <mutex>

#include "session_teardown.hxx"

namespace forge::net::p2p::detail {

class session_retirement {
 public:
   enum class close_start {
      started,
      terminal,
      in_flight,
      untracked,
   };

   session_retirement();
   ~session_retirement();

   session_retirement(const session_retirement&) = delete;
   session_retirement& operator=(const session_retirement&) = delete;

   [[nodiscard]] bool track(session_teardown::ticket ticket) noexcept;
   [[nodiscard]] bool tracked() const noexcept;
   [[nodiscard]] bool terminal() const noexcept;

   [[nodiscard]] close_start begin_close(bool allow_untracked) noexcept;
   // Recheck begin_close after waking: another caller may already own the retry.
   // Keep this object alive until the wait completes.
   boost::asio::awaitable<void> async_wait_not_in_flight();
   // Call only after native close, resource release and registry erasure.
   [[nodiscard]] bool complete_terminal(session_teardown::ticket& ticket) noexcept;
   void quarantine() noexcept;

 private:
   forge::asio::notification changed_;
   mutable std::mutex mutex_;
   session_teardown::ticket ticket_;
   bool terminal_ = false;
   bool close_in_flight_ = false;
};

} // namespace forge::net::p2p::detail

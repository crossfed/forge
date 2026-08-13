#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <string>

namespace forge::net::p2p::detail {

class pubsub_backoff {
 public:
   using clock = std::chrono::steady_clock;

   enum class status {
      none,
      exact,
      saturated,
   };

   [[nodiscard]] static std::size_t limit_for(std::size_t max_topics, std::size_t max_sessions) noexcept;
   [[nodiscard]] static std::chrono::seconds remote_duration(std::chrono::seconds requested,
                                                              std::chrono::seconds fallback) noexcept;

   void expire(clock::time_point now) noexcept;
   void record_local(const std::string& topic, const peer_id& peer, std::chrono::seconds duration,
                     clock::time_point now, std::size_t limit) noexcept;
   void record_remote(const std::string& topic, const peer_id& peer, std::chrono::seconds duration,
                      clock::time_point now, std::size_t limit) noexcept;

   [[nodiscard]] status local_status(const std::string& topic, const peer_id& peer,
                                     clock::time_point now) const noexcept;
   [[nodiscard]] status remote_status(const std::string& topic, const peer_id& peer,
                                      clock::time_point now) const noexcept;
   [[nodiscard]] bool blocked(const std::string& topic, const peer_id& peer, clock::time_point now) const noexcept;
   [[nodiscard]] std::size_t size() const noexcept;

 private:
   struct entry {
      clock::time_point local_until{};
      clock::time_point remote_until{};
   };

   enum class direction {
      local,
      remote,
   };

   [[nodiscard]] static clock::time_point deadline(clock::time_point now,
                                                    std::chrono::seconds duration) noexcept;
   void record(direction kind, const std::string& topic, const peer_id& peer, std::chrono::seconds duration,
               clock::time_point now, std::size_t limit) noexcept;
   [[nodiscard]] status get_status(direction kind, const std::string& topic, const peer_id& peer,
                                   clock::time_point now) const noexcept;

   std::map<std::string, std::map<peer_id, entry>> entries_;
   std::size_t size_ = 0;
   clock::time_point local_saturated_until_{};
   clock::time_point remote_saturated_until_{};
};

} // namespace forge::net::p2p::detail

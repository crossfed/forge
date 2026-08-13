#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace forge::net::p2p::detail {

class lifecycle_wakeup;

class dht_provider_registry final : public std::enable_shared_from_this<dht_provider_registry> {
   struct owner_state;

 public:
   struct schedule {
      std::chrono::seconds provider_ttl{};
      std::chrono::seconds address_ttl{};
      std::chrono::seconds republish_interval{};
   };

   struct callbacks {
      std::function<std::shared_ptr<void>()> track;
      std::function<bool(std::function<boost::asio::awaitable<void>()>)> launch;
      std::function<boost::asio::awaitable<dht::peer>(protocol_id, dht::key, schedule)> prepare;
      std::function<boost::asio::awaitable<std::size_t>(protocol_id, dht::key, dht::peer, dht::query_options)> publish;
      std::function<boost::asio::awaitable<void>(protocol_id, dht::key)> remove;
      std::function<std::size_t(const protocol_id&)> publication_limit;
   };

   class lease {
    public:
      lease() = default;

    private:
      explicit lease(std::shared_ptr<owner_state> owner_value);

      std::shared_ptr<owner_state> owner_;

      friend class dht_provider_registry;
   };

   explicit dht_provider_registry(callbacks callbacks_value);
   ~dht_provider_registry();

   dht_provider_registry(const dht_provider_registry&) = delete;
   dht_provider_registry& operator=(const dht_provider_registry&) = delete;

   boost::asio::awaitable<lease> async_acquire(protocol_id protocol, dht::key key, dht::query_options query,
                                               schedule renewal);
   [[nodiscard]] bool active(const lease& value) const noexcept;
   void request_release(const lease& value) noexcept;
   boost::asio::awaitable<void> async_release(const lease& value);
   void open_admission() noexcept;
   void notify_endpoints_changed() noexcept;
   void seal() noexcept;
   boost::asio::awaitable<void> async_drain();

 private:
   struct registration_key {
      protocol_id protocol;
      dht::key key;

      [[nodiscard]] friend auto operator<=>(const registration_key&, const registration_key&) noexcept = default;
   };

   struct owner_state {
      std::uint64_t id = 0;
      registration_key registration;
      std::shared_ptr<lifecycle_wakeup> changed;
      mutable std::mutex mutex;
      std::exception_ptr terminal_failure;
      bool release_requested = false;
      bool terminal = false;
   };

   struct entry {
      registration_key registration;
      dht::query_options query;
      schedule renewal;
      std::map<std::uint64_t, std::weak_ptr<owner_state>> owners;
      std::chrono::steady_clock::time_point next_republish{};
      std::uint64_t observed_endpoint_generation = 0;
      std::uint32_t publish_failures = 0;
      std::exception_ptr removal_failure;
      bool stop_requested = false;
      bool removal_in_flight = false;
      bool removal_failed = false;
   };

   [[nodiscard]] std::shared_ptr<owner_state> add_owner_locked(const std::shared_ptr<entry>& value);
   [[nodiscard]] std::size_t profile_entry_count_locked(const protocol_id& protocol) const;
   void request_release_owner(const std::shared_ptr<owner_state>& owner) noexcept;
   boost::asio::awaitable<void> async_release_owner(const std::shared_ptr<owner_state>& owner);
   boost::asio::awaitable<void> async_run(std::shared_ptr<entry> value);
   boost::asio::awaitable<void> async_remove(const std::shared_ptr<entry>& value);
   void reset_owners_for_retry(const std::shared_ptr<entry>& value) noexcept;
   boost::asio::awaitable<void> async_rollback(const registration_key& registration);
   boost::asio::awaitable<std::size_t> async_publish(protocol_id protocol, dht::key key, dht::peer provider,
                                                     dht::query_options query);
   void release_publication(const protocol_id& protocol) noexcept;
   void finish_entry(const std::shared_ptr<entry>& value, std::exception_ptr failure) noexcept;
   static void finish_owner(const std::shared_ptr<owner_state>& owner, std::exception_ptr failure) noexcept;
   static boost::asio::awaitable<void> async_wait_owner(const std::shared_ptr<owner_state>& owner);
   [[nodiscard]] std::chrono::milliseconds republish_delay(const entry& value) const noexcept;
   [[nodiscard]] std::chrono::milliseconds retry_delay(const entry& value) const noexcept;

   static constexpr std::size_t max_entries_per_profile = 1'024;

   callbacks callbacks_;
   std::shared_ptr<forge::asio::gate> admission_;
   std::shared_ptr<lifecycle_wakeup> changed_;
   mutable std::mutex mutex_;
   std::map<registration_key, std::shared_ptr<entry>> entries_;
   std::map<protocol_id, std::size_t> active_publications_;
   std::exception_ptr drain_failure_;
   std::uint64_t next_owner_id_ = 1;
   std::uint64_t endpoint_generation_ = 1;
   std::size_t admissions_in_flight_ = 0;
   bool admission_open_ = false;
   bool sealed_ = false;
};

} // namespace forge::net::p2p::detail

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace forge::net::p2p {

struct dht::record_store::impl {
   class operation_admission;
   class close_admission;

   using value_key = std::vector<std::uint8_t>;
   using provider_map_key = std::pair<value_key, peer_id>;

   impl(dht::profile profile_value, dht::record_store::options options_value);

   boost::asio::awaitable<dht::record_store::put_result> async_put(dht::record_store::value_record incoming,
                                                                   std::chrono::system_clock::time_point now);
   boost::asio::awaitable<void> async_upsert_provider(dht::record_store::provider_record value,
                                                      std::chrono::system_clock::time_point now);
   boost::asio::awaitable<void> async_remove_provider(dht::record_store::provider_key key);
   boost::asio::awaitable<void> async_hydrate(std::chrono::system_clock::time_point now);
   boost::asio::awaitable<dht::record_store::prune_result>
   async_prune_expired(std::chrono::system_clock::time_point now);
   boost::asio::awaitable<void> async_flush();
   boost::asio::awaitable<void> async_close();

   static boost::asio::awaitable<dht::record_store::put_result>
   async_put_owned(std::shared_ptr<impl> self, dht::record_store::value_record incoming,
                   std::chrono::system_clock::time_point now);
   static boost::asio::awaitable<void> async_upsert_provider_owned(std::shared_ptr<impl> self,
                                                                   dht::record_store::provider_record value,
                                                                   std::chrono::system_clock::time_point now);
   static boost::asio::awaitable<void> async_remove_provider_owned(std::shared_ptr<impl> self,
                                                                   dht::record_store::provider_key key);
   static boost::asio::awaitable<void> async_hydrate_owned(std::shared_ptr<impl> self,
                                                           std::chrono::system_clock::time_point now);
   static boost::asio::awaitable<dht::record_store::prune_result>
   async_prune_expired_owned(std::shared_ptr<impl> self, std::chrono::system_clock::time_point now);
   static boost::asio::awaitable<void> async_flush_owned(std::shared_ptr<impl> self);
   static boost::asio::awaitable<void> async_close_owned(std::shared_ptr<impl> self);

   [[nodiscard]] std::optional<dht::record_store::value_record>
   find_value(const dht::key& key, std::chrono::system_clock::time_point now) const;
   [[nodiscard]] std::vector<dht::record_store::provider_record>
   find_providers(const dht::key& key, std::size_t limit, std::chrono::system_clock::time_point now) const;
   [[nodiscard]] dht::record_store::persistence_status persistence_state() const;

 private:
   [[nodiscard]] operation_admission admit_operation();
   [[nodiscard]] std::optional<close_admission> admit_close();
   void release_operation() noexcept;
   void release_close() noexcept;
   boost::asio::awaitable<void> wait_for_operations();

   [[nodiscard]] static bool expired(std::chrono::system_clock::time_point value,
                                     std::chrono::system_clock::time_point now) noexcept;
   [[nodiscard]] static bool exceeds(std::size_t current, std::size_t removed, std::size_t added,
                                     std::size_t maximum) noexcept;
   [[nodiscard]] static std::string current_failure_message();
   [[nodiscard]] static std::string durability_failure_message(const dht::record_store::apply_result& result);
   [[noreturn]] static void throw_durability_uncertain(const dht::record_store::apply_result& result);

   void ensure_open_locked() const;
   [[nodiscard]] const dht::value_policy& prepare_value(dht::record_store::value_record& value,
                                                        std::chrono::system_clock::time_point now) const;
   void validate_provider(const dht::record_store::provider_record& value,
                          std::chrono::system_clock::time_point now) const;
   [[nodiscard]] std::size_t value_bytes(const dht::record_store::value_record& value) const;
   [[nodiscard]] std::size_t provider_bytes(const dht::record_store::provider_record& value) const;
   void ensure_value_capacity_locked(const dht::record_store::value_record& value) const;
   void ensure_provider_capacity_locked(const dht::record_store::provider_record& value) const;
   void publish_value_locked(dht::record_store::value_record value);
   void publish_provider_locked(dht::record_store::provider_record value);
   void erase_value_locked(const dht::key& key);
   void erase_provider_locked(const dht::record_store::provider_key& key);
   void apply_prune_locked(const dht::record_store::prune_result& result);
   void mark_persistence_failure_locked(std::string message);
   void mark_durability_uncertain_locked(std::string message);
   void mark_persistence_healthy_locked(bool durability_confirmed = false);
   void apply_durability_result_locked(const dht::record_store::apply_result& result);

   dht::profile profile_;
   dht::record_store::options options_;
   std::shared_ptr<dht::record_store::persistence> persistence_;
   mutable std::mutex mutex_;
   forge::asio::gate persistence_gate_;
   std::map<value_key, dht::record_store::value_record> values_;
   std::map<provider_map_key, dht::record_store::provider_record> providers_;
   std::map<value_key, std::set<peer_id>> providers_by_key_;
   std::size_t local_providers_ = 0;
   std::size_t total_bytes_ = 0;
   std::size_t operation_admissions_ = 0;
   std::map<const void*, std::function<void()>> operation_drainers_;
   std::size_t close_waiters_ = 0;
   std::uint64_t persistence_failures_ = 0;
   bool degraded_ = false;
   bool durability_uncertain_ = false;
   bool closing_ = false;
   bool closed_ = false;
   std::string last_failure_;
};

class dht::record_store::impl::operation_admission {
 public:
   explicit operation_admission(dht::record_store::impl* owner) noexcept;
   ~operation_admission();

   operation_admission(const operation_admission&) = delete;
   operation_admission& operator=(const operation_admission&) = delete;
   operation_admission(operation_admission&& other) noexcept;
   operation_admission& operator=(operation_admission&& other) noexcept;

 private:
   dht::record_store::impl* owner_ = nullptr;
};

class dht::record_store::impl::close_admission {
 public:
   explicit close_admission(dht::record_store::impl* owner) noexcept;
   ~close_admission();

   close_admission(const close_admission&) = delete;
   close_admission& operator=(const close_admission&) = delete;
   close_admission(close_admission&& other) noexcept;
   close_admission& operator=(close_admission&& other) noexcept;

 private:
   dht::record_store::impl* owner_ = nullptr;
};

} // namespace forge::net::p2p

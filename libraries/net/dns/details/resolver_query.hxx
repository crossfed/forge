#pragma once

#include <ares.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include "resolver_socket_registry.hxx"

namespace forge::net::dns {

class cares_library;

class resolver_query final : public std::enable_shared_from_this<resolver_query> {
 public:
   [[nodiscard]] static std::shared_ptr<resolver_query>
   make_address_query(std::shared_ptr<resolver::impl> owner, std::uint64_t id, std::string name,
                      address_family family, query_options options);
   [[nodiscard]] static std::shared_ptr<resolver_query>
   make_text_query(std::shared_ptr<resolver::impl> owner, std::uint64_t id, std::string name,
                   query_options options);

   void start();
   void abandon_start() noexcept;
   void request_cancel_from_any_thread() noexcept;
   void request_cancel_on_strand() noexcept;
   void request_close_on_strand() noexcept;
   [[nodiscard]] boost::asio::awaitable<address_response> async_wait_addresses();
   [[nodiscard]] boost::asio::awaitable<text_response> async_wait_text();
   void release_owner() noexcept;

 private:
   enum class response_kind : std::uint8_t {
      addresses,
      text,
   };

   struct callback_context {
      std::shared_ptr<resolver_query> operation;
      ares_dns_rec_type_t submitted_type;
   };

   struct cname_edge {
      std::string owner;
      std::string target;
      std::string display_target;
   };

   enum class query_outcome : std::uint8_t {
      pending,
      usable,
      not_found,
      failure,
   };

   struct submitted_query {
      query_outcome outcome = query_outcome::pending;
      exceptions::code error_code = exceptions::code::internal;
      std::exception_ptr error;
   };

   resolver_query(std::shared_ptr<resolver::impl> owner, std::uint64_t id, std::string name,
                  std::vector<ares_dns_rec_type_t> record_types, response_kind kind, query_options options);

   static void socket_state_callback(void* opaque, ares_socket_t fd, int readable, int writable) noexcept;
   static void query_callback(void* opaque, ares_status_t status, std::size_t timeouts,
                              const ares_dns_record_t* record) noexcept;

   void on_socket_state(ares_socket_t fd, bool readable, bool writable);
   void on_query_callback(callback_context* context, ares_status_t status, const ares_dns_record_t* record,
                          ares_dns_rec_type_t submitted_type) noexcept;
   void finish_submission_failure(callback_context* context, ares_status_t status) noexcept;
   void decode_record(const ares_dns_record_t* record, ares_dns_rec_type_t submitted_type);
   void preflight_cname_limits(const ares_dns_record_t* record);
   [[nodiscard]] std::vector<std::string> terminal_owners(const ares_dns_record_t* record);
   void decode_cname(const cname_edge& edge);
   void decode_ipv4(const ares_dns_rr_t* rr);
   void decode_ipv6(const ares_dns_rr_t* rr);
   void decode_text(const ares_dns_rr_t* rr);
   void add_address(boost::asio::ip::address value, std::size_t bytes, unsigned int ttl);
   void consume_record_bytes(std::size_t size) const;
   void consume_total_bytes(std::size_t size);
   [[nodiscard]] std::vector<std::string>& canonical_names();
   void finish_success_if_ready();
   void finish_error(std::exception_ptr value) noexcept;
   void request_terminal(std::exception_ptr error) noexcept;
   void schedule_drain() noexcept;
   void begin_drain() noexcept;
   void arm_watch(const std::shared_ptr<resolver_socket_watch>& watch);
   void on_socket_ready(const std::shared_ptr<resolver_socket_watch>& watch, bool readable,
                        std::uint64_t wait_generation, const boost::system::error_code& error) noexcept;
   void arm_all_watches();
   void release_watch(ares_socket_t fd) noexcept;
   void release_all_borrowed() noexcept;
   void arm_timer();
   void on_timer(std::uint64_t generation, const boost::system::error_code& error) noexcept;
   [[nodiscard]] boost::asio::awaitable<void> wait_for_completion();
   void try_retire() noexcept;
   [[noreturn]] void throw_status(int status);

   std::shared_ptr<resolver::impl> owner;
   const boost::asio::any_io_executor callback_executor;
   const std::uint64_t id;
   const std::string name;
   const std::vector<ares_dns_rec_type_t> record_types;
   const response_kind kind;
   const query_options options;
   std::shared_ptr<cares_library> library;
   ares_channel_t* channel = nullptr;
   boost::asio::steady_timer timer;
   resolver_socket_registry socket_registry;
   std::shared_ptr<forge::asio::notification> completion;
   std::unordered_map<callback_context*, std::shared_ptr<callback_context>> callback_contexts;
   std::optional<address_response> addresses;
   std::optional<text_response> text;
   std::vector<submitted_query> submitted_queries;
   std::exception_ptr error;
   std::size_t total_bytes = 0;
   std::size_t callbacks_in_flight = 0;
   std::size_t pending_handlers = 0;
   std::uint64_t timer_generation = 0;
   std::string requested_name;
   bool submissions_complete = false;
   bool completed = false;
   bool cancel_sent = false;
   bool drain_scheduled = false;
   bool draining = false;
   bool retired = false;
};

} // namespace forge::net::dns

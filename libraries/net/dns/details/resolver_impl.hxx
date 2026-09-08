#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/strand.hpp>

namespace forge::net::dns {

class resolver_query;

struct resolver::impl final : std::enable_shared_from_this<resolver::impl> {
   explicit impl(boost::asio::any_io_executor executor, resolver_options options);
   ~impl();

   static boost::asio::awaitable<address_response>
   async_resolve_addresses(std::shared_ptr<impl> state, std::string name, address_family family,
                           query_options options, std::stop_token stop);
   static boost::asio::awaitable<text_response>
   async_resolve_txt(std::shared_ptr<impl> state, std::string name, query_options options, std::stop_token stop);
   static boost::asio::awaitable<void> async_close(std::shared_ptr<impl> state);

   [[nodiscard]] std::size_t active_queries() const noexcept;
   void request_cancel() noexcept;
   void request_close() noexcept;

 private:
   enum class terminal_reason : std::uint8_t {
      canceled,
      closed,
   };

   friend class resolver_query;

   void cancel_all(terminal_reason reason) noexcept;
   void close_on_strand() noexcept;
   void retire(std::uint64_t id, const std::shared_ptr<resolver_query>& operation) noexcept;

   boost::asio::strand<boost::asio::any_io_executor> strand;
   resolver_options options;
   std::unordered_map<std::uint64_t, std::shared_ptr<resolver_query>> operations;
   std::shared_ptr<forge::asio::notification> close_changed;
   std::uint64_t next_operation_id = 1;
   std::atomic_size_t active_count = 0;
   bool closing = false;
};

} // namespace forge::net::dns

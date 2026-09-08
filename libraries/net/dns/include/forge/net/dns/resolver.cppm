module;

#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module forge.net.dns.resolver;

export import forge.net.dns.exceptions;
export import forge.net.dns.types;

export namespace forge::net::dns {

class resolver {
 public:
   explicit resolver(boost::asio::any_io_executor executor, resolver_options options = {});
   ~resolver();

   resolver(resolver&&) noexcept;
   resolver& operator=(resolver&&) noexcept;

   resolver(const resolver&) = delete;
   resolver& operator=(const resolver&) = delete;

   [[nodiscard]] boost::asio::awaitable<address_response>
   async_resolve_addresses(std::string name, address_family family = address_family::any,
                           query_options options = {}, std::stop_token stop = {});
   [[nodiscard]] boost::asio::awaitable<text_response>
   async_resolve_txt(std::string name, query_options options = {}, std::stop_token stop = {});
   [[nodiscard]] std::size_t active_queries() const noexcept;
   void request_cancel() noexcept;
   [[nodiscard]] boost::asio::awaitable<void> async_close();

 private:
   friend class resolver_query;

   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::net::dns

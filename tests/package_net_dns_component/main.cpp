#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/asio/io_context.hpp>

import forge.net.dns.exceptions;
import forge.net.dns.resolver;
import forge.net.dns.types;

static_assert(
    std::is_same_v<decltype(std::declval<const forge::net::dns::resolver&>().active_queries()), std::size_t>);

int main() {
   auto context = boost::asio::io_context{};
   auto options = forge::net::dns::resolver_options{
       .nameservers = {{.address = "127.0.0.1", .port = 53, .scope_id = ""}},
       .max_in_flight = 1,
   };
   auto resolver = forge::net::dns::resolver{context.get_executor(), std::move(options)};
   const auto query = forge::net::dns::query_options{
       .max_answers = 1,
       .max_cnames = 0,
       .max_record_bytes = 16,
       .max_total_answer_bytes = 16,
   };
   auto operation = resolver.async_resolve_addresses("example.test", forge::net::dns::address_family::ipv4, query);
   static_cast<void>(operation);
   return resolver.active_queries() == 0 ? 0 : 1;
}

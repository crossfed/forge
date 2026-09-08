#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

namespace forge::net::p2p::detail {

enum class dns_address_branch_failure {
   none,
   not_found,
   temporary_failure,
};

struct dns_address_expansion_state {
   const address_resolution::limits& limits;
   std::size_t dns_lookups = 0;
   std::vector<endpoint> results;
   std::set<std::string> result_keys;
   std::set<std::string> active;
   dns_address_branch_failure strongest_branch_failure = dns_address_branch_failure::none;
};

class dns_address_expander final {
 public:
   using address_lookup = std::function<boost::asio::awaitable<forge::net::dns::address_response>(
       std::string, forge::net::dns::address_family, forge::net::dns::query_options, std::stop_token)>;
   using text_lookup = std::function<boost::asio::awaitable<forge::net::dns::text_response>(
       std::string, forge::net::dns::query_options, std::stop_token)>;

   struct resolver_callbacks {
      address_lookup resolve_addresses;
      text_lookup resolve_txt;
   };

   dns_address_expander(address_resolution::policy policy, resolver_callbacks callbacks);
   dns_address_expander(forge::net::dns::resolver& resolver, address_resolution::policy policy);

   [[nodiscard]] boost::asio::awaitable<std::vector<endpoint>>
   async_expand(forge::multiformats::multiaddr value, std::optional<peer_id> expected_peer,
                std::chrono::steady_clock::time_point deadline, std::stop_token stop = {});

 private:
   address_resolution::policy policy_;
   resolver_callbacks callbacks_;
};

} // namespace forge::net::p2p::detail

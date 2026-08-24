#pragma once

namespace forge::plugins::p2p::resolver::detail {

struct catalog_ticket {
   forge::net::p2p::protocol_id protocol;
   std::uint64_t generation = 0;
};

struct catalog_publication {
   forge::net::p2p::protocol_id protocol;
   std::vector<entry> entries;
   forge::api::p2p::publication route_publication;
   std::function<void()> close_handle;
   std::uint64_t generation = 0;
};

} // namespace forge::plugins::p2p::resolver::detail

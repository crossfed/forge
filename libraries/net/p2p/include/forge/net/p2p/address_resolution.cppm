module;

#include <cstddef>

export module forge.net.p2p.address_resolution;

export namespace forge::net::p2p {

struct address_resolution {
   struct limits {
      std::size_t max_dns_lookups = 32;
      std::size_t max_txt_records = 16;
      std::size_t max_resolved_addresses = 100;
      std::size_t max_recursion_depth = 4;
      std::size_t max_multiaddr_size = 4096;
   };

   struct policy {
      limits bounds{};
   };
};

} // namespace forge::net::p2p

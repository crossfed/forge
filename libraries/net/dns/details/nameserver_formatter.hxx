#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace forge::net::dns::detail {

struct nameserver_format_view {
   std::string_view address;
   std::uint16_t port = 0;
   std::string_view scope_id;
   bool ipv6 = false;
};

[[nodiscard]] std::string format_nameserver_csv(std::span<const nameserver_format_view> servers);

} // namespace forge::net::dns::detail

module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/asio/ip/address.hpp>

export module forge.net.dns.types;

export namespace forge::net::dns {

enum class address_family : std::uint8_t {
   any,
   ipv4,
   ipv6,
};

struct nameserver {
   std::string address;
   std::uint16_t port = 53;
   std::string scope_id;
};

struct resolver_options {
   std::vector<nameserver> nameservers;
   std::size_t max_in_flight = 16;
};

struct query_options {
   std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
   std::size_t max_answers = 16;
   std::size_t max_cnames = 8;
   std::size_t max_record_bytes = 4096;
   std::size_t max_total_answer_bytes = 65536;
};

struct address_answer {
   boost::asio::ip::address value;
   std::chrono::seconds ttl{};
};

struct address_response {
   std::vector<address_answer> answers;
   std::vector<std::string> canonical_names;
};

struct text_answer {
   std::vector<std::uint8_t> value;
   std::chrono::seconds ttl{};
};

struct text_response {
   std::vector<text_answer> answers;
   std::vector<std::string> canonical_names;
};

} // namespace forge::net::dns

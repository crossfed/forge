#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>

namespace forge::tests::dns {

using bytes = std::vector<std::uint8_t>;

struct parsed_question {
   std::uint16_t id = 0;
   std::string name;
   std::uint16_t type = 0;
   std::size_t end = 0;
};

struct response_answer {
   std::uint16_t type = 0;
   bytes value;
   std::uint32_t ttl = 60;
   std::string owner;
};

[[nodiscard]] std::optional<parsed_question> parse_question(const std::uint8_t* data, std::size_t size);
[[nodiscard]] bytes encoded_name(std::string_view name);
[[nodiscard]] bytes make_response(const std::uint8_t* request, const parsed_question& question,
                                  const std::vector<response_answer>& answers);
[[nodiscard]] bytes make_failure_response(const std::uint8_t* request, const parsed_question& question,
                                          std::uint16_t response_code);

// Test-driver-owned UDP service. Handlers run serially and may return no reply.
// close()/destruction must run outside the handler; both join all callbacks.
class local_dns_server final {
 public:
   using answer_handler = std::function<std::optional<bytes>(const std::uint8_t*, std::size_t)>;

   explicit local_dns_server(answer_handler handler);
   ~local_dns_server();
   local_dns_server(const local_dns_server&) = delete;
   local_dns_server& operator=(const local_dns_server&) = delete;

   [[nodiscard]] std::uint16_t port() const noexcept;
   // Also rethrows a handler/service error, after joining. Idempotent on success.
   void close();

 private:
   struct service {
      explicit service(answer_handler handler);
      void request_stop() noexcept;
      void run() noexcept;
      static void receive(std::shared_ptr<service> self);

      boost::asio::io_context context;
      boost::asio::strand<boost::asio::io_context::executor_type> strand;
      boost::asio::ip::udp::socket socket;
      std::array<std::uint8_t, 512> request{};
      boost::asio::ip::udp::endpoint peer;
      answer_handler handler;
      std::exception_ptr error;
      bool stopping = false;
   };

   std::shared_ptr<service> _service;
   std::uint16_t _port;
   std::thread _worker;
};

} // namespace forge::tests::dns

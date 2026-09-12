#include "local_dns_server.hxx"

#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace forge::tests::dns {
namespace {

void append_u16(bytes& out, std::uint16_t value) {
   out.push_back(static_cast<std::uint8_t>(value >> 8U));
   out.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(bytes& out, std::uint32_t value) {
   out.push_back(static_cast<std::uint8_t>(value >> 24U));
   out.push_back(static_cast<std::uint8_t>(value >> 16U));
   out.push_back(static_cast<std::uint8_t>(value >> 8U));
   out.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

std::optional<parsed_question> parse_question(const std::uint8_t* data, std::size_t size) {
   if (size < 17) {
      return std::nullopt;
   }
   auto result = parsed_question{.id = static_cast<std::uint16_t>((data[0] << 8U) | data[1])};
   auto offset = std::size_t{12};
   while (offset < size) {
      const auto label_size = data[offset++];
      if (label_size == 0) {
         break;
      }
      if ((label_size & 0xC0U) != 0 || label_size > size - offset) {
         return std::nullopt;
      }
      if (!result.name.empty()) {
         result.name.push_back('.');
      }
      result.name.append(reinterpret_cast<const char*>(data + offset), label_size);
      offset += label_size;
   }
   if (offset + 4 > size) {
      return std::nullopt;
   }
   result.type = static_cast<std::uint16_t>((data[offset] << 8U) | data[offset + 1]);
   result.end = offset + 4;
   return result;
}

bytes encoded_name(std::string_view name) {
   auto out = bytes{};
   while (!name.empty()) {
      const auto separator = name.find('.');
      const auto label = name.substr(0, separator);
      out.push_back(static_cast<std::uint8_t>(label.size()));
      out.insert(out.end(), label.begin(), label.end());
      if (separator == std::string_view::npos) {
         break;
      }
      name.remove_prefix(separator + 1);
   }
   out.push_back(0);
   return out;
}

bytes make_response(const std::uint8_t* request, const parsed_question& question,
                    const std::vector<response_answer>& answers) {
   auto out = bytes{};
   out.reserve(question.end + answers.size() * 32);
   append_u16(out, question.id);
   append_u16(out, 0x8180);
   append_u16(out, 1);
   append_u16(out, static_cast<std::uint16_t>(answers.size()));
   append_u16(out, 0);
   append_u16(out, 0);
   out.insert(out.end(), request + 12, request + question.end);
   for (const auto& answer : answers) {
      if (answer.owner.empty()) {
         append_u16(out, 0xC00C);
      } else {
         const auto owner = encoded_name(answer.owner);
         out.insert(out.end(), owner.begin(), owner.end());
      }
      append_u16(out, answer.type);
      append_u16(out, 1);
      append_u32(out, answer.ttl);
      append_u16(out, static_cast<std::uint16_t>(answer.value.size()));
      out.insert(out.end(), answer.value.begin(), answer.value.end());
   }
   return out;
}

bytes make_failure_response(const std::uint8_t* request, const parsed_question& question,
                            std::uint16_t response_code) {
   auto out = make_response(request, question, {});
   out[2] = static_cast<std::uint8_t>(out[2] | ((response_code >> 8U) & 0x0FU));
   out[3] = static_cast<std::uint8_t>((out[3] & 0xF0U) | (response_code & 0x0FU));
   return out;
}

local_dns_server::service::service(answer_handler handler_value)
    : strand(boost::asio::make_strand(context)),
      socket(strand, boost::asio::ip::udp::endpoint{boost::asio::ip::address_v4::loopback(), 0}),
      handler(std::move(handler_value)) {}

void local_dns_server::service::request_stop() noexcept {
   stopping = true;
   auto ignored = boost::system::error_code{};
   socket.cancel(ignored);
   socket.close(ignored);
}

void local_dns_server::service::run() noexcept {
   for (;;) {
      try {
         context.run();
         return;
      } catch (...) {
         if (!error) {
            error = std::current_exception();
         }
         request_stop();
         // An escaping handler does not stop io_context; drain canceled work.
      }
   }
}

void local_dns_server::service::receive(std::shared_ptr<service> self) {
   if (self->stopping) {
      return;
   }
   self->socket.async_receive_from(
       boost::asio::buffer(self->request), self->peer,
       [self](const boost::system::error_code& error, std::size_t size) {
          if (self->stopping) {
             return;
          }
          if (error) {
             throw boost::system::system_error{error};
          }
          if (auto answer = self->handler(self->request.data(), size)) {
             auto payload = std::make_shared<bytes>(std::move(*answer));
             self->socket.async_send_to(
                 boost::asio::buffer(*payload), self->peer,
                 [self, payload](const boost::system::error_code& send_error, std::size_t) {
                    if (self->stopping) {
                       return;
                    }
                    if (send_error) {
                       throw boost::system::system_error{send_error};
                    }
                    receive(self);
                 });
          } else {
             receive(self);
          }
       });
}

local_dns_server::local_dns_server(answer_handler handler)
    : _service(std::make_shared<service>(std::move(handler))),
      _port(_service->socket.local_endpoint().port()) {
   // Initiate on the owned thread so construction failure cannot strand a
   // self-owning pending receive before the thread exists.
   _worker = std::thread{[self = _service] {
      try {
         service::receive(self);
      } catch (...) {
         self->error = std::current_exception();
         self->request_stop();
      }
      self->run();
   }};
}

local_dns_server::~local_dns_server() {
   try {
      close();
   } catch (...) {
   }
}

std::uint16_t local_dns_server::port() const noexcept {
   return _port;
}

void local_dns_server::close() {
   if (_worker.joinable()) {
      try {
         boost::asio::post(_service->strand, [self = _service] { self->request_stop(); });
      } catch (...) {
         // Even allocation failure posting stop must not leave a live thread
         // or a self-owning callback cycle. Only touch the socket after join.
         _service->context.stop();
         _worker.join();
         _service->request_stop();
         _service->context.restart();
         _service->run();
         _service->handler = {};
         throw;
      }
      _worker.join();
      // The service may have exited on an error just before stop was posted.
      _service->context.restart();
      _service->run();
      _service->handler = {};
   }
   if (_service->error) {
      std::rethrow_exception(_service->error);
   }
}

} // namespace forge::tests::dns

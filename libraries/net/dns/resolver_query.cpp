module;

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <ares.h>

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#if defined(_WIN32)
#include <boost/asio/generic/datagram_protocol.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#include <sys/time.h>
#endif

#include "details/nameserver_formatter.hxx"

module forge.net.dns.resolver;

import forge.asio.notification;

#include "details/cares_library.hxx"
#include "details/resolver_impl.hxx"
#include "details/resolver_query.hxx"
#include "details/resolver_socket_watch.hxx"
#include "details/resolver_wait_failure.hxx"

namespace forge::net::dns {
namespace {

namespace asio = boost::asio;

inline constexpr auto maximum_answers = std::size_t{4096};
inline constexpr auto maximum_cnames = std::size_t{1024};
inline constexpr auto maximum_record_bytes = std::size_t{64U * 1024U};
inline constexpr auto maximum_total_answer_bytes = std::size_t{1024U * 1024U};

[[noreturn]] void throw_invalid_options(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, message);
}

[[noreturn]] void throw_resource_limit(std::string_view limit, std::size_t maximum, std::size_t actual) {
   FORGE_THROW_EXCEPTION(exceptions::resource_limit, "dns response exceeded configured resource limit",
                         forge::exceptions::ctx("limit", limit), forge::exceptions::ctx("maximum", maximum),
                         forge::exceptions::ctx("actual", actual));
}

[[nodiscard]] std::exception_ptr make_exception(exceptions::code value, std::string_view reason) noexcept {
   try {
      switch (value) {
      case exceptions::code::canceled:
         FORGE_THROW_EXCEPTION(exceptions::canceled, "dns query was canceled", forge::exceptions::ctx("reason", reason));
      case exceptions::code::closed:
         FORGE_THROW_EXCEPTION(exceptions::closed, "dns resolver is closed", forge::exceptions::ctx("reason", reason));
      case exceptions::code::timeout:
         FORGE_THROW_EXCEPTION(exceptions::timeout, "dns query timed out", forge::exceptions::ctx("reason", reason));
      case exceptions::code::not_found:
         FORGE_THROW_EXCEPTION(exceptions::not_found, "dns record was not found", forge::exceptions::ctx("reason", reason));
      case exceptions::code::temporary_failure:
         FORGE_THROW_EXCEPTION(exceptions::temporary_failure, "dns query failed temporarily",
                               forge::exceptions::ctx("reason", reason));
      case exceptions::code::codec_error:
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "dns response could not be decoded",
                               forge::exceptions::ctx("reason", reason));
      case exceptions::code::resource_limit:
         FORGE_THROW_EXCEPTION(exceptions::resource_limit, "dns response exceeded configured resource limit",
                               forge::exceptions::ctx("reason", reason));
      case exceptions::code::internal:
         FORGE_THROW_EXCEPTION(exceptions::internal, "dns resolver internal failure",
                               forge::exceptions::ctx("reason", reason));
      case exceptions::code::invalid_options:
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "dns resolver received invalid options",
                               forge::exceptions::ctx("reason", reason));
      }
   } catch (...) {
      return std::current_exception();
   }
   return {};
}

[[nodiscard]] std::exception_ptr make_status_exception(int status) noexcept {
   switch (status) {
   case ARES_ENODATA:
   case ARES_ENOTFOUND:
   case ARES_ENONAME:
      return make_exception(exceptions::code::not_found, "dns server returned NXDOMAIN or NODATA");
   case ARES_EFORMERR:
   case ARES_EBADRESP:
      return make_exception(exceptions::code::codec_error, "dns server returned malformed response data");
   case ARES_ETIMEOUT:
      return make_exception(exceptions::code::timeout, "dns server did not answer before its timeout");
   case ARES_ESERVFAIL:
   case ARES_EREFUSED:
   case ARES_ECONNREFUSED:
   case ARES_ENOSERVER:
      return make_exception(exceptions::code::temporary_failure, "dns server did not provide a retryable answer");
   case ARES_ENOMEM:
      return make_exception(exceptions::code::resource_limit, "c-ares could not allocate query state");
   default:
      return make_exception(exceptions::code::internal, "c-ares returned an unexpected status");
   }
}

[[nodiscard]] exceptions::code status_code(int status) noexcept {
   switch (status) {
   case ARES_ENODATA:
   case ARES_ENOTFOUND:
   case ARES_ENONAME:
      return exceptions::code::not_found;
   case ARES_EFORMERR:
   case ARES_EBADRESP:
      return exceptions::code::codec_error;
   case ARES_ETIMEOUT:
      return exceptions::code::timeout;
   case ARES_ESERVFAIL:
   case ARES_EREFUSED:
   case ARES_ECONNREFUSED:
   case ARES_ENOSERVER:
      return exceptions::code::temporary_failure;
   case ARES_ENOMEM:
      return exceptions::code::resource_limit;
   default:
      return exceptions::code::internal;
   }
}

[[nodiscard]] bool is_hard_terminal_failure(exceptions::code value) noexcept {
   return value == exceptions::code::codec_error || value == exceptions::code::resource_limit;
}

[[nodiscard]] bool is_not_found_status(int status) noexcept {
   return status == ARES_ENODATA || status == ARES_ENOTFOUND || status == ARES_ENONAME;
}

[[nodiscard]] std::optional<std::string> normalize_rr_name(const char* value) {
   if (!value) {
      return std::nullopt;
   }
   auto result = std::string{value};
   if (result.empty() || result.size() > 253 || result.find('\0') != std::string::npos) {
      return std::nullopt;
   }
   if (result.back() == '.') {
      result.pop_back();
   }
   if (result.empty()) {
      return std::nullopt;
   }
   auto label_begin = std::size_t{0};
   for (auto index = std::size_t{}; index <= result.size(); ++index) {
      if (index == result.size() || result[index] == '.') {
         if (index == label_begin || index - label_begin > 63) {
            return std::nullopt;
         }
         label_begin = index + 1;
      } else {
         const auto value_byte = static_cast<unsigned char>(result[index]);
         if (value_byte < 0x21U || value_byte > 0x7EU) {
            return std::nullopt;
         }
         if (value_byte >= 'A' && value_byte <= 'Z') {
            result[index] = static_cast<char>(value_byte - 'A' + 'a');
         }
      }
   }
   return result;
}

[[nodiscard]] std::string normalize_query_name(std::string value) {
   if (!value.empty() && value.back() == '.') {
      value.pop_back();
   }
   for (auto& character : value) {
      const auto value_byte = static_cast<unsigned char>(character);
      if (value_byte >= 'A' && value_byte <= 'Z') {
         character = static_cast<char>(value_byte - 'A' + 'a');
      }
   }
   return value;
}

void validate_query(std::string_view name, const query_options& options) {
   if (name.empty() || name.size() > 253 || name.find('\0') != std::string_view::npos) {
      throw_invalid_options("dns name must be non-empty, contain no NUL and be at most 253 bytes");
   }
   if (options.max_answers == 0 || options.max_answers > maximum_answers) {
      throw_invalid_options("dns max_answers must be between 1 and 4096");
   }
   if (options.max_cnames > maximum_cnames) {
      throw_invalid_options("dns max_cnames must be at most 1024");
   }
   if (options.max_record_bytes == 0 || options.max_record_bytes > maximum_record_bytes) {
      throw_invalid_options("dns max_record_bytes must be between 1 and 65536");
   }
   if (options.max_total_answer_bytes == 0 || options.max_total_answer_bytes > maximum_total_answer_bytes) {
      throw_invalid_options("dns max_total_answer_bytes must be between 1 and 1048576");
   }
}

} // namespace

std::shared_ptr<resolver_query>
resolver_query::make_address_query(std::shared_ptr<resolver::impl> owner, std::uint64_t id, std::string name,
                                   address_family family, query_options options) {
   auto record_types = std::vector<ares_dns_rec_type_t>{};
   if (family == address_family::any || family == address_family::ipv4) {
      record_types.push_back(ARES_REC_TYPE_A);
   }
   if (family == address_family::any || family == address_family::ipv6) {
      record_types.push_back(ARES_REC_TYPE_AAAA);
   }
   return std::shared_ptr<resolver_query>{
       new resolver_query{std::move(owner), id, std::move(name), std::move(record_types), response_kind::addresses,
                          std::move(options)}};
}

std::shared_ptr<resolver_query>
resolver_query::make_text_query(std::shared_ptr<resolver::impl> owner, std::uint64_t id, std::string name,
                                query_options options) {
   return std::shared_ptr<resolver_query>{new resolver_query{
       std::move(owner), id, std::move(name), {ARES_REC_TYPE_TXT}, response_kind::text, std::move(options)}};
}

resolver_query::resolver_query(std::shared_ptr<resolver::impl> owner_value, std::uint64_t id_value,
                               std::string name_value, std::vector<ares_dns_rec_type_t> record_types_value,
                               response_kind kind_value, query_options options_value)
    : owner(std::move(owner_value)), callback_executor(owner->strand), id(id_value), name(std::move(name_value)),
      record_types(std::move(record_types_value)), kind(kind_value), options(std::move(options_value)), timer(owner->strand),
      socket_registry(callback_executor), completion(std::make_shared<forge::asio::notification>()),
      addresses(kind == response_kind::addresses ? std::optional<address_response>{address_response{}} : std::nullopt),
      text(kind == response_kind::text ? std::optional<text_response>{text_response{}} : std::nullopt),
      submitted_queries(record_types.size()) {}

void resolver_query::start() {
   validate_query(name, options);
   requested_name = normalize_query_name(name);
   if (options.deadline != std::chrono::steady_clock::time_point::max() &&
       options.deadline <= std::chrono::steady_clock::now()) {
      finish_error(make_exception(exceptions::code::timeout, "query deadline elapsed before dispatch"));
      return;
   }

   library = cares_library::acquire();
   auto init_options = ares_options{};
   init_options.sock_state_cb = &resolver_query::socket_state_callback;
   init_options.sock_state_cb_data = this;
   const auto init_status = ares_init_options(&channel, &init_options, ARES_OPT_SOCK_STATE_CB);
   if (init_status != ARES_SUCCESS) {
      channel = nullptr;
      throw_status(init_status);
   }

   if (!owner->options.nameservers.empty()) {
      auto views = std::vector<detail::nameserver_format_view>{};
      views.reserve(owner->options.nameservers.size());
      for (const auto& server : owner->options.nameservers) {
         views.push_back({.address = server.address,
                          .port = server.port,
                          .scope_id = server.scope_id,
                          .ipv6 = server.address.find(':') != std::string::npos});
      }
      const auto servers = detail::format_nameserver_csv(views);
      const auto server_status = ares_set_servers_ports_csv(channel, servers.c_str());
      if (server_status != ARES_SUCCESS) {
         throw_status(server_status);
      }
   }

   for (const auto record_type : record_types) {
      if (completed) {
         break;
      }
      auto context = std::make_shared<callback_context>(callback_context{shared_from_this(), record_type});
      auto* raw_context = context.get();
      callback_contexts.emplace(raw_context, std::move(context));
      ++callbacks_in_flight;
      const auto status = ares_query_dnsrec(channel, name.c_str(), ARES_CLASS_IN, record_type,
                                             &resolver_query::query_callback, raw_context, nullptr);
      if (status != ARES_SUCCESS) {
         finish_submission_failure(raw_context, status);
         break;
      }
   }
   submissions_complete = true;
   finish_success_if_ready();
   arm_timer();
}

void resolver_query::abandon_start() noexcept {
   if (channel) {
      release_all_borrowed();
      ares_destroy(channel);
      channel = nullptr;
   }
   library.reset();
}

void resolver_query::request_cancel_from_any_thread() noexcept {
   auto self = shared_from_this();
   try {
      asio::post(callback_executor, [self = std::move(self)] { self->request_cancel_on_strand(); });
   } catch (...) {
   }
}

void resolver_query::request_cancel_on_strand() noexcept {
   request_terminal(make_exception(exceptions::code::canceled, "resolver cancellation was requested"));
}

void resolver_query::request_close_on_strand() noexcept {
   request_terminal(make_exception(exceptions::code::closed, "resolver close was requested"));
}

boost::asio::awaitable<address_response> resolver_query::async_wait_addresses() {
   co_await wait_for_completion();
   if (error) {
      std::rethrow_exception(error);
   }
   co_return std::move(*addresses);
}

boost::asio::awaitable<text_response> resolver_query::async_wait_text() {
   co_await wait_for_completion();
   if (error) {
      std::rethrow_exception(error);
   }
   co_return std::move(*text);
}

void resolver_query::release_owner() noexcept {
   owner.reset();
   library.reset();
}

void resolver_query::socket_state_callback(void* opaque, ares_socket_t fd, int readable, int writable) noexcept {
   auto* operation = static_cast<resolver_query*>(opaque);
   try {
      operation->on_socket_state(fd, readable != 0, writable != 0);
   } catch (...) {
      // Do not re-enter c-ares from its socket callback. Drain after it returns.
      operation->finish_error(std::current_exception());
   }
}

void resolver_query::query_callback(void* opaque, ares_status_t status, std::size_t,
                                    const ares_dns_record_t* record) noexcept {
   try {
      auto* context = static_cast<callback_context*>(opaque);
      context->operation->on_query_callback(context, status, record, context->submitted_type);
   } catch (...) {
   }
}

void resolver_query::on_query_callback(callback_context* context, ares_status_t status, const ares_dns_record_t* record,
                                       ares_dns_rec_type_t submitted_type) noexcept {
   const auto registered = callback_contexts.find(context);
   if (registered == callback_contexts.end()) {
      return;
   }
   auto keep_alive = std::move(registered->second);
   callback_contexts.erase(registered);
   --callbacks_in_flight;
   if (completed) {
      return;
   }
   const auto iterator = std::find(record_types.begin(), record_types.end(), submitted_type);
   if (iterator == record_types.end()) {
      finish_error(make_exception(exceptions::code::internal, "c-ares callback reported an unknown query type"));
      return;
   }
   auto& submitted = submitted_queries[static_cast<std::size_t>(iterator - record_types.begin())];
   if (status == ARES_SUCCESS) {
      try {
         if (!record) {
            finish_error(make_exception(exceptions::code::codec_error, "c-ares returned a null DNS record"));
            return;
         }
         const auto answers_before = kind == response_kind::addresses ? addresses->answers.size() : text->answers.size();
         decode_record(record, submitted_type);
         const auto answers_after = kind == response_kind::addresses ? addresses->answers.size() : text->answers.size();
         submitted.outcome = answers_after > answers_before ? query_outcome::usable : query_outcome::not_found;
      } catch (...) {
         finish_error(std::current_exception());
         return;
      }
   } else if (is_not_found_status(status)) {
      submitted.outcome = query_outcome::not_found;
   } else {
      submitted.outcome = query_outcome::failure;
      submitted.error_code = status_code(status);
      submitted.error = make_status_exception(status);
      if (is_hard_terminal_failure(submitted.error_code)) {
         finish_error(submitted.error);
         return;
      }
   }
   finish_success_if_ready();
}

void resolver_query::finish_submission_failure(callback_context* context, ares_status_t status) noexcept {
   const auto registered = callback_contexts.find(context);
   if (registered == callback_contexts.end()) {
      return;
   }
   callback_contexts.erase(registered);
   --callbacks_in_flight;
   finish_error(make_status_exception(status));
}

void resolver_query::decode_record(const ares_dns_record_t* record, ares_dns_rec_type_t submitted_type) {
   const auto terminals = terminal_owners(record);
   const auto count = ares_dns_record_rr_cnt(record, ARES_SECTION_ANSWER);
   for (auto index = std::size_t{}; index < count; ++index) {
      const auto* rr = ares_dns_record_rr_get_const(record, ARES_SECTION_ANSWER, index);
      if (!rr) {
         throw_status(ARES_EBADRESP);
      }
      if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN) {
         continue;
      }

      const auto type = ares_dns_rr_get_type(rr);
      if (type == ARES_REC_TYPE_CNAME) {
         continue;
      }
      const auto owner = normalize_rr_name(ares_dns_rr_get_name(rr));
      if (!owner) {
         throw_status(ARES_EBADRESP);
      }
      if (std::find(terminals.begin(), terminals.end(), *owner) == terminals.end()) {
         continue;
      }
      if (kind == response_kind::addresses && submitted_type == ARES_REC_TYPE_A && type == ARES_REC_TYPE_A) {
         decode_ipv4(rr);
      } else if (kind == response_kind::addresses && submitted_type == ARES_REC_TYPE_AAAA && type == ARES_REC_TYPE_AAAA) {
         decode_ipv6(rr);
      } else if (kind == response_kind::text && submitted_type == ARES_REC_TYPE_TXT && type == ARES_REC_TYPE_TXT) {
         decode_text(rr);
      }
   }
}

void resolver_query::preflight_cname_limits(const ares_dns_record_t* record) {
   auto cname_count = std::size_t{};
   auto cname_bytes = std::size_t{};
   const auto count = ares_dns_record_rr_cnt(record, ARES_SECTION_ANSWER);
   for (auto index = std::size_t{}; index < count; ++index) {
      const auto* rr = ares_dns_record_rr_get_const(record, ARES_SECTION_ANSWER, index);
      if (!rr) {
         throw_status(ARES_EBADRESP);
      }
      if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN || ares_dns_rr_get_type(rr) != ARES_REC_TYPE_CNAME) {
         continue;
      }
      if (cname_count >= options.max_cnames) {
         throw_resource_limit("max_cnames", options.max_cnames, cname_count + 1);
      }
      ++cname_count;

      const auto* owner = ares_dns_rr_get_name(rr);
      const auto* target = ares_dns_rr_get_str(rr, ARES_RR_CNAME_CNAME);
      if (!owner || !target) {
         throw_status(ARES_EBADRESP);
      }
      const auto owner_bytes = std::string_view{owner}.size();
      const auto target_bytes = std::string_view{target}.size();
      const auto record_bytes = target_bytes > std::numeric_limits<std::size_t>::max() - owner_bytes
                                    ? std::numeric_limits<std::size_t>::max()
                                    : owner_bytes + target_bytes;
      if (record_bytes > options.max_record_bytes) {
         throw_resource_limit("max_record_bytes", options.max_record_bytes, record_bytes);
      }
      if (cname_bytes > options.max_total_answer_bytes ||
          record_bytes > options.max_total_answer_bytes - cname_bytes) {
         const auto actual = record_bytes > std::numeric_limits<std::size_t>::max() - cname_bytes
                                 ? std::numeric_limits<std::size_t>::max()
                                 : cname_bytes + record_bytes;
         throw_resource_limit("max_total_answer_bytes", options.max_total_answer_bytes, actual);
      }
      cname_bytes += record_bytes;
   }
}

std::vector<std::string> resolver_query::terminal_owners(const ares_dns_record_t* record) {
   preflight_cname_limits(record);
   auto edges = std::vector<cname_edge>{};
   const auto count = ares_dns_record_rr_cnt(record, ARES_SECTION_ANSWER);
   for (auto index = std::size_t{}; index < count; ++index) {
      const auto* rr = ares_dns_record_rr_get_const(record, ARES_SECTION_ANSWER, index);
      if (!rr) {
         throw_status(ARES_EBADRESP);
      }
      if (ares_dns_rr_get_class(rr) != ARES_CLASS_IN || ares_dns_rr_get_type(rr) != ARES_REC_TYPE_CNAME) {
         continue;
      }
      const auto owner = normalize_rr_name(ares_dns_rr_get_name(rr));
      const auto* target_value = ares_dns_rr_get_str(rr, ARES_RR_CNAME_CNAME);
      const auto target = normalize_rr_name(target_value);
      if (!owner || !target || !target_value) {
         throw_status(ARES_EBADRESP);
      }
      edges.push_back({.owner = *owner, .target = *target, .display_target = target_value});
   }

   auto reachable = std::vector<std::string>{requested_name};
   auto accepted = std::vector<cname_edge>{};
   auto progressed = true;
   while (progressed) {
      progressed = false;
      for (const auto& edge : edges) {
         if (std::find(reachable.begin(), reachable.end(), edge.owner) == reachable.end()) {
            continue;
         }
         const auto existing = std::find_if(accepted.begin(), accepted.end(), [&edge](const auto& candidate) {
            return candidate.owner == edge.owner;
         });
         if (existing != accepted.end()) {
            if (existing->target != edge.target) {
               throw_status(ARES_EBADRESP);
            }
            continue;
         }
         if (std::find(reachable.begin(), reachable.end(), edge.target) != reachable.end()) {
            throw_status(ARES_EBADRESP);
         }
         accepted.push_back(edge);
         reachable.push_back(edge.target);
         decode_cname(edge);
         progressed = true;
      }
   }
   auto terminals = std::vector<std::string>{};
   for (const auto& candidate : reachable) {
      const auto has_outgoing_cname = std::any_of(accepted.begin(), accepted.end(), [&candidate](const auto& edge) {
         return edge.owner == candidate;
      });
      if (!has_outgoing_cname) {
         terminals.push_back(candidate);
      }
   }
   return terminals;
}

void resolver_query::decode_cname(const cname_edge& edge) {
   const auto already_exposed = std::any_of(canonical_names().begin(), canonical_names().end(), [&edge](const auto& value) {
      const auto normalized = normalize_rr_name(value.c_str());
      return normalized && *normalized == edge.target;
   });
   if (already_exposed) {
      return;
   }
   if (canonical_names().size() >= options.max_cnames) {
      throw_resource_limit("max_cnames", options.max_cnames, canonical_names().size() + 1);
   }
   consume_record_bytes(edge.display_target.size());
   consume_total_bytes(edge.display_target.size());
   canonical_names().push_back(edge.display_target);
}

void resolver_query::decode_ipv4(const ares_dns_rr_t* rr) {
   const auto* value = ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR);
   if (!value) {
      throw_status(ARES_EBADRESP);
   }
   auto bytes = std::array<unsigned char, 4>{};
   std::memcpy(bytes.data(), value, bytes.size());
   add_address(asio::ip::address_v4{bytes}, bytes.size(), ares_dns_rr_get_ttl(rr));
}

void resolver_query::decode_ipv6(const ares_dns_rr_t* rr) {
   const auto* value = ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
   if (!value) {
      throw_status(ARES_EBADRESP);
   }
   auto bytes = std::array<unsigned char, 16>{};
   std::memcpy(bytes.data(), value, bytes.size());
   add_address(asio::ip::address_v6{bytes}, bytes.size(), ares_dns_rr_get_ttl(rr));
}

void resolver_query::add_address(asio::ip::address value, std::size_t bytes, unsigned int ttl) {
   if (addresses->answers.size() >= options.max_answers) {
      throw_resource_limit("max_answers", options.max_answers, addresses->answers.size() + 1);
   }
   consume_record_bytes(bytes);
   consume_total_bytes(bytes);
   addresses->answers.push_back(
       {.value = std::move(value), .ttl = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(ttl)}});
}

void resolver_query::decode_text(const ares_dns_rr_t* rr) {
   if (text->answers.size() >= options.max_answers) {
      throw_resource_limit("max_answers", options.max_answers, text->answers.size() + 1);
   }
   auto value = std::vector<std::uint8_t>{};
   const auto segments = ares_dns_rr_get_abin_cnt(rr, ARES_RR_TXT_DATA);
   for (auto index = std::size_t{}; index < segments; ++index) {
      auto length = std::size_t{};
      const auto* segment = ares_dns_rr_get_abin(rr, ARES_RR_TXT_DATA, index, &length);
      if (!segment && length != 0) {
         throw_status(ARES_EBADRESP);
      }
      if (length > 255) {
         throw_status(ARES_EBADRESP);
      }
      if (length > options.max_record_bytes - value.size()) {
         throw_resource_limit("max_record_bytes", options.max_record_bytes, value.size() + length);
      }
      if (length > options.max_total_answer_bytes - total_bytes - value.size()) {
         throw_resource_limit("max_total_answer_bytes", options.max_total_answer_bytes,
                              total_bytes + value.size() + length);
      }
      if (length != 0) {
         value.insert(value.end(), segment, segment + length);
      }
   }
   consume_record_bytes(value.size());
   consume_total_bytes(value.size());
   text->answers.push_back(
       {.value = std::move(value),
        .ttl = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(ares_dns_rr_get_ttl(rr))}});
}

void resolver_query::consume_record_bytes(std::size_t size) const {
   if (size > options.max_record_bytes) {
      throw_resource_limit("max_record_bytes", options.max_record_bytes, size);
   }
}

void resolver_query::consume_total_bytes(std::size_t size) {
   if (size > options.max_total_answer_bytes - total_bytes) {
      throw_resource_limit("max_total_answer_bytes", options.max_total_answer_bytes, total_bytes + size);
   }
   total_bytes += size;
}

std::vector<std::string>& resolver_query::canonical_names() {
   return kind == response_kind::addresses ? addresses->canonical_names : text->canonical_names;
}

void resolver_query::finish_success_if_ready() {
   if (completed || !submissions_complete || callbacks_in_flight != 0) {
      return;
   }
   const auto answer_count = kind == response_kind::addresses ? addresses->answers.size() : text->answers.size();
   if (answer_count != 0) {
      completed = true;
      completion->notify();
      schedule_drain();
      return;
   }
   for (const auto priority : {exceptions::code::timeout, exceptions::code::temporary_failure,
                               exceptions::code::internal}) {
      for (const auto& submitted : submitted_queries) {
         if (submitted.outcome == query_outcome::failure && submitted.error_code == priority) {
            finish_error(submitted.error);
            return;
         }
      }
   }
   finish_error(make_exception(exceptions::code::not_found, "DNS response contained no requested records"));
}

void resolver_query::finish_error(std::exception_ptr value) noexcept {
   if (completed) {
      return;
   }
   error = std::move(value);
   completed = true;
   completion->notify();
   schedule_drain();
}

void resolver_query::request_terminal(std::exception_ptr terminal_error) noexcept {
   if (completed) {
      return;
   }
   finish_error(std::move(terminal_error));
   if (channel && !cancel_sent) {
      cancel_sent = true;
      ares_cancel(channel);
   }
}

void resolver_query::schedule_drain() noexcept {
   if (drain_scheduled || !owner) {
      return;
   }
   drain_scheduled = true;
   auto self = shared_from_this();
   try {
      asio::post(owner->strand, [self = std::move(self)] { self->begin_drain(); });
   } catch (...) {
   }
}

void resolver_query::begin_drain() noexcept {
   if (draining) {
      return;
   }
   draining = true;
   ++timer_generation;
   try {
      timer.cancel();
   } catch (...) {
   }
   release_all_borrowed();
   if (channel) {
      if (error && !cancel_sent) {
         cancel_sent = true;
         ares_cancel(channel);
      }
      ares_destroy(channel);
      channel = nullptr;
   }
   try_retire();
}

void resolver_query::on_socket_state(ares_socket_t fd, bool readable, bool writable) {
   if (draining) {
      if (!readable && !writable) {
         release_watch(fd);
      }
      return;
   }
   if (!readable && !writable) {
      release_watch(fd);
      return;
   }
   auto watch = socket_registry.create_or_find(fd);
   watch->set_interest(readable, writable);
   arm_watch(watch);
}

void resolver_query::arm_watch(const std::shared_ptr<resolver_socket_watch>& watch) {
   if (const auto generation = watch->begin_read_wait()) {
      ++pending_handlers;
      try {
         resolver_wait_failure::check(resolver_wait_failure::point::read);
         auto self = shared_from_this();
         watch->async_wait_read([self = std::move(self), watch, generation = *generation](const boost::system::error_code& error) {
            self->on_socket_ready(watch, true, generation, error);
         });
      } catch (...) {
         --pending_handlers;
         static_cast<void>(watch->complete_read_wait(*generation));
         throw;
      }
   }
   if (const auto generation = watch->begin_write_wait()) {
      ++pending_handlers;
      try {
         resolver_wait_failure::check(resolver_wait_failure::point::write);
         auto self = shared_from_this();
         watch->async_wait_write([self = std::move(self), watch, generation = *generation](const boost::system::error_code& error) {
            self->on_socket_ready(watch, false, generation, error);
         });
      } catch (...) {
         --pending_handlers;
         static_cast<void>(watch->complete_write_wait(*generation));
         throw;
      }
   }
}

void resolver_query::on_socket_ready(const std::shared_ptr<resolver_socket_watch>& watch, bool readable,
                                     std::uint64_t wait_generation,
                                     const boost::system::error_code& event_error) noexcept {
   if (pending_handlers != 0) {
      --pending_handlers;
   }
   const auto current_wait = readable ? watch->complete_read_wait(wait_generation)
                                      : watch->complete_write_wait(wait_generation);
   if (!current_wait || !socket_registry.contains(watch->fd(), watch)) {
      try_retire();
      return;
   }
   if (event_error != asio::error::operation_aborted && !completed && !draining && channel) {
      const auto event = ares_fd_events_t{.fd = watch->fd(),
                                          .events = static_cast<unsigned int>(readable ? ARES_FD_EVENT_READ
                                                                                       : ARES_FD_EVENT_WRITE)};
      const auto status = ares_process_fds(channel, &event, 1, ARES_PROCESS_FLAG_NONE);
      if (status != ARES_SUCCESS) {
         finish_error(make_status_exception(status));
      }
   }
   if (!completed && !draining) {
      try {
         arm_all_watches();
         arm_timer();
      } catch (...) {
         finish_error(std::current_exception());
      }
   }
   try_retire();
}

void resolver_query::arm_all_watches() {
   for (const auto& watch : socket_registry.snapshot()) {
      arm_watch(watch);
   }
}

void resolver_query::release_watch(ares_socket_t fd) noexcept {
   socket_registry.release(fd);
}

void resolver_query::release_all_borrowed() noexcept {
   socket_registry.release_all();
}

void resolver_query::arm_timer() {
   if (completed || draining || !channel) {
      return;
   }
   auto timeout = timeval{};
   const auto* next = ares_timeout(channel, nullptr, &timeout);
   auto deadline = std::chrono::steady_clock::time_point::max();
   if (next) {
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds{timeout.tv_sec} +
                 std::chrono::microseconds{timeout.tv_usec};
   }
   deadline = std::min(deadline, options.deadline);
   if (deadline == std::chrono::steady_clock::time_point::max()) {
      return;
   }
   const auto generation = ++timer_generation;
   timer.expires_at(deadline);
   ++pending_handlers;
   try {
      resolver_wait_failure::check(resolver_wait_failure::point::timer);
      auto self = shared_from_this();
      timer.async_wait([self = std::move(self), generation](const boost::system::error_code& timer_error) {
         self->on_timer(generation, timer_error);
      });
   } catch (...) {
      --pending_handlers;
      throw;
   }
}

void resolver_query::on_timer(std::uint64_t generation, const boost::system::error_code& timer_error) noexcept {
   if (pending_handlers != 0) {
      --pending_handlers;
   }
   if (generation != timer_generation || timer_error || completed || draining || !channel) {
      try_retire();
      return;
   }
   if (options.deadline != std::chrono::steady_clock::time_point::max() &&
       std::chrono::steady_clock::now() >= options.deadline) {
      request_terminal(make_exception(exceptions::code::timeout, "query deadline elapsed"));
      try_retire();
      return;
   }
   const auto status = ares_process_fds(channel, nullptr, 0, ARES_PROCESS_FLAG_NONE);
   if (status != ARES_SUCCESS) {
      finish_error(make_status_exception(status));
   } else if (!completed) {
      try {
         arm_all_watches();
         arm_timer();
      } catch (...) {
         finish_error(std::current_exception());
      }
   }
   try_retire();
}

boost::asio::awaitable<void> resolver_query::wait_for_completion() {
   while (!completed) {
      const auto observed = completion->epoch();
      if (!completed) {
         static_cast<void>(co_await completion->async_wait(observed));
      }
   }
}

void resolver_query::try_retire() noexcept {
   if (!draining || retired || callbacks_in_flight != 0 || pending_handlers != 0 || !owner) {
      return;
   }
   retired = true;
   owner->retire(id, shared_from_this());
}

[[noreturn]] void resolver_query::throw_status(int status) {
   std::rethrow_exception(make_status_exception(status));
}

} // namespace forge::net::dns

module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <coroutine>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

module forge.net.http.file;

import forge.net.http.body;
import forge.net.http.exceptions;
import forge.exceptions;
import forge.net.http.range;
import forge.net.http.types;

namespace forge::net::http {
namespace {

[[nodiscard]] std::string http_date(std::time_t value) {
   auto tm = std::tm{};
#if defined(_WIN32)
   gmtime_s(&tm, &value);
#else
   gmtime_r(&value, &tm);
#endif
   auto output = std::ostringstream{};
   output << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
   return output.str();
}

[[nodiscard]] std::string file_etag(std::uint64_t size, std::int64_t seconds, std::int64_t nanoseconds) {
   auto output = std::ostringstream{};
   output << "W/\"" << size << '-' << seconds << '-' << nanoseconds << '"';
   return output.str();
}

[[nodiscard]] std::optional<std::string> header_value(const request& request_value, field name) {
   const auto found = request_value.find(name);
   if (found == request_value.end()) {
      return std::nullopt;
   }
   return std::string{found->value()};
}

[[nodiscard]] std::optional<std::string_view> view_of(const std::optional<std::string>& value) {
   if (!value.has_value()) {
      return std::nullopt;
   }
   return std::string_view{*value};
}

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept {
   while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.remove_prefix(1U);
   }
   while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
      value.remove_suffix(1U);
   }
   return value;
}

[[nodiscard]] std::optional<std::string_view> opaque_etag(std::string_view value) noexcept {
   value = trim_ows(value);
   if (value.starts_with("W/")) {
      value.remove_prefix(2U);
   }
   if (value.size() < 2U || value.front() != '"' || value.back() != '"') {
      return std::nullopt;
   }
   return value;
}

[[nodiscard]] bool weak_etag_matches(std::string_view left, std::string_view right) noexcept {
   const auto left_tag = opaque_etag(left);
   const auto right_tag = opaque_etag(right);
   return left_tag.has_value() && right_tag.has_value() && *left_tag == *right_tag;
}

[[nodiscard]] bool strong_etag_matches(std::string_view left, std::string_view right) noexcept {
   left = trim_ows(left);
   right = trim_ows(right);
   return !left.starts_with("W/") && !right.starts_with("W/") && weak_etag_matches(left, right);
}

[[nodiscard]] std::size_t next_etag_separator(std::string_view value) noexcept {
   auto quoted = false;
   for (auto index = std::size_t{0}; index != value.size(); ++index) {
      if (value[index] == '"') {
         quoted = !quoted;
      } else if (value[index] == ',' && !quoted) {
         return index;
      }
   }
   return std::string_view::npos;
}

[[nodiscard]] bool if_none_match_matches(std::string_view value, std::string_view etag_value) noexcept {
   while (true) {
      const auto separator = next_etag_separator(value);
      const auto candidate = trim_ows(value.substr(0, separator));
      if (candidate == "*" || weak_etag_matches(candidate, etag_value)) {
         return true;
      }
      if (separator == std::string_view::npos) {
         return false;
      }
      value.remove_prefix(separator + 1U);
   }
}

[[nodiscard]] std::optional<std::time_t> parse_http_date(std::string_view value) {
   auto input = std::istringstream{std::string{trim_ows(value)}};
   auto parsed = std::tm{};
   input >> std::get_time(&parsed, "%a, %d %b %Y %H:%M:%S GMT");
   if (input.fail()) {
      return std::nullopt;
   }
   auto trailing = std::string{};
   if (input >> trailing) {
      return std::nullopt;
   }
#if defined(_WIN32)
   return _mkgmtime(&parsed);
#else
   return timegm(&parsed);
#endif
}

[[nodiscard]] bool if_modified_since_matches(std::string_view value, std::int64_t modified_seconds) {
   const auto date = parse_http_date(value);
   return date.has_value() && modified_seconds <= static_cast<std::int64_t>(*date);
}

[[nodiscard]] bool if_range_matches(std::string_view value, std::string_view etag_value,
                                    std::int64_t modified_seconds, const file_options& options) {
   value = trim_ows(value);
   if (value.starts_with("W/") || value.starts_with("\"")) {
      return options.etag && strong_etag_matches(value, etag_value);
   }
   return options.last_modified && if_modified_since_matches(value, modified_seconds);
}

void throw_file_write_failed(const std::filesystem::path& target, std::string_view phase) {
   FORGE_THROW_EXCEPTION(exceptions::internal, "failed to save HTTP file response",
                         forge::exceptions::ctx("path", target.string()),
                         forge::exceptions::ctx("phase", std::string{phase}));
}

[[nodiscard]] std::vector<std::string> split_relative_path(std::string_view value) {
   if (value.find('\\') != std::string_view::npos || value.find('\0') != std::string_view::npos) {
      throw exceptions::forbidden{"unsafe static file path"};
   }

   auto segments = std::vector<std::string>{};
   auto start = std::size_t{0};
   while (start <= value.size()) {
      const auto separator = value.find('/', start);
      const auto end = separator == std::string_view::npos ? value.size() : separator;
      const auto segment = value.substr(start, end - start);
      if (segment.empty() || segment == "." || segment == "..") {
         throw exceptions::forbidden{"unsafe static file path"};
      }
      segments.emplace_back(segment);
      if (separator == std::string_view::npos) {
         break;
      }
      start = separator + 1U;
   }
   return segments;
}

[[nodiscard]] std::shared_ptr<int> own_descriptor(int descriptor) {
   return std::shared_ptr<int>{new int{descriptor}, [](int* value) {
                                  if (value != nullptr) {
                                     if (*value >= 0) {
                                        static_cast<void>(::close(*value));
                                     }
                                     delete value;
                                  }
                               }};
}

[[noreturn]] void throw_open_error(int error) {
   if (error == ENOENT) {
      throw exceptions::not_found{"file not found"};
   }
   if (error == ELOOP || error == ENOTDIR || error == EACCES || error == EPERM) {
      throw exceptions::forbidden{"static file path is not allowed"};
   }
   FORGE_THROW_EXCEPTION(exceptions::internal, "failed to open HTTP file");
}

[[nodiscard]] std::shared_ptr<int> open_file(const std::filesystem::path& path, symlink_policy symlinks) {
   auto flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
   if (symlinks == symlink_policy::reject) {
      flags |= O_NOFOLLOW;
   }
   const auto descriptor = ::open(path.c_str(), flags);
   if (descriptor < 0) {
      throw_open_error(errno);
   }
   return own_descriptor(descriptor);
}

[[nodiscard]] std::shared_ptr<int> open_relative_file(const std::shared_ptr<int>& root,
                                                      std::string_view relative_path) {
   if (!root || *root < 0 || relative_path.empty() || relative_path.front() == '/') {
      throw exceptions::forbidden{"unsafe static file path"};
   }
   const auto duplicate = ::dup(*root);
   if (duplicate < 0) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "failed to duplicate static file root descriptor");
   }
   auto current = own_descriptor(duplicate);
   const auto segments = split_relative_path(relative_path);
   for (auto index = std::size_t{0}; index != segments.size(); ++index) {
      auto flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
      if (index + 1U != segments.size()) {
         flags |= O_DIRECTORY;
      } else {
         flags |= O_NONBLOCK;
      }
      const auto descriptor = ::openat(*current, segments[index].c_str(), flags);
      if (descriptor < 0) {
         throw_open_error(errno);
      }
      current = own_descriptor(descriptor);
   }
   return current;
}

[[nodiscard]] bool path_contains(const std::filesystem::path& root, const std::filesystem::path& child) {
   auto root_it = root.begin();
   auto child_it = child.begin();
   for (; root_it != root.end(); ++root_it, ++child_it) {
      if (child_it == child.end() || *root_it != *child_it) {
         return false;
      }
   }
   return true;
}

[[nodiscard]] std::shared_ptr<int> open_followed_relative_file(const std::filesystem::path& root,
                                                               const std::shared_ptr<int>& root_descriptor,
                                                               std::string_view relative_path) {
   if (relative_path.empty() || relative_path.front() == '/') {
      throw exceptions::forbidden{"unsafe static file path"};
   }

   auto candidate = root;
   for (const auto& segment : split_relative_path(relative_path)) {
      candidate /= segment;
   }
   auto error = std::error_code{};
   const auto resolved = std::filesystem::weakly_canonical(candidate, error);
   if (error) {
      if (error == std::errc::no_such_file_or_directory) {
         throw exceptions::not_found{"file not found"};
      }
      throw exceptions::forbidden{"static file path is not allowed"};
   }
   if (!path_contains(root, resolved)) {
      throw exceptions::forbidden{"static file path escapes root"};
   }
   const auto resolved_relative = resolved.lexically_relative(root);
   if (resolved_relative.empty() || resolved_relative == ".") {
      throw exceptions::forbidden{"static file path is not allowed"};
   }
   return open_relative_file(root_descriptor, resolved_relative.generic_string());
}

struct opened_file_metadata {
   std::uint64_t size = 0;
   std::int64_t modified_seconds = 0;
   std::int64_t modified_nanoseconds = 0;
};

[[nodiscard]] opened_file_metadata inspect_open_file(const std::shared_ptr<int>& descriptor,
                                                     const file_options& options) {
   struct stat metadata{};
   if (!descriptor || *descriptor < 0 || ::fstat(*descriptor, &metadata) != 0) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "failed to inspect opened HTTP file");
   }
   if (!S_ISREG(metadata.st_mode)) {
      throw exceptions::not_found{"file not found"};
   }
   if (metadata.st_size < 0) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "opened HTTP file has an invalid size");
   }
   const auto size = static_cast<std::uint64_t>(metadata.st_size);
   if (size > options.max_file_bytes) {
      throw exceptions::payload_too_large{"HTTP file exceeds the configured size limit"};
   }
#if defined(__APPLE__)
   return {.size = size,
           .modified_seconds = metadata.st_mtimespec.tv_sec,
           .modified_nanoseconds = metadata.st_mtimespec.tv_nsec};
#else
   return {.size = size, .modified_seconds = metadata.st_mtim.tv_sec, .modified_nanoseconds = metadata.st_mtim.tv_nsec};
#endif
}

[[nodiscard]] stream_response not_modified_response(const request& request_value, const std::string& etag_value,
                                                    const std::string& modified_value, const file_options& options) {
   auto reply = response{status::not_modified, request_value.version()};
   if (options.etag) {
      reply.set(field::etag, etag_value);
   }
   if (options.last_modified) {
      reply.set(field::last_modified, modified_value);
   }
   if (!options.cache_control.empty()) {
      reply.set("Cache-Control", options.cache_control);
   }
   reply.keep_alive(request_value.keep_alive());
   return stream_response::buffered(std::move(reply));
}

[[nodiscard]] stream_response make_file_stream(const request& request_value, const std::shared_ptr<int>& descriptor,
                                               const opened_file_metadata& metadata, const file_options& options) {
   const auto modified_value = http_date(static_cast<std::time_t>(metadata.modified_seconds));
   const auto etag_value = file_etag(metadata.size, metadata.modified_seconds, metadata.modified_nanoseconds);
   if (options.etag) {
      const auto if_none_match = header_value(request_value, field::if_none_match);
      if (if_none_match.has_value()) {
         if (if_none_match_matches(*if_none_match, etag_value)) {
            return not_modified_response(request_value, etag_value, modified_value, options);
         }
      } else if (options.last_modified) {
         const auto if_modified_since = header_value(request_value, field::if_modified_since);
         if (if_modified_since.has_value() &&
             if_modified_since_matches(*if_modified_since, metadata.modified_seconds)) {
            return not_modified_response(request_value, etag_value, modified_value, options);
         }
      }
   } else if (options.last_modified) {
      const auto if_modified_since = header_value(request_value, field::if_modified_since);
      if (if_modified_since.has_value() && if_modified_since_matches(*if_modified_since, metadata.modified_seconds)) {
         return not_modified_response(request_value, etag_value, modified_value, options);
      }
   }

   const auto range_header = header_value(request_value, field::range);
   auto range_value = resolve_range(view_of(range_header), metadata.size);
   if (range_header.has_value()) {
      const auto if_range = header_value(request_value, field::if_range);
      if (if_range.has_value() && !if_range_matches(*if_range, etag_value, metadata.modified_seconds, options)) {
         range_value = resolve_range(std::nullopt, metadata.size);
      }
   }

   auto reply = response{range_value.partial ? status::partial_content : status::ok, request_value.version()};
   reply.set(field::content_type, options.content_type);
   reply.set(field::accept_ranges, "bytes");
   reply.keep_alive(request_value.keep_alive());

   if (options.etag) {
      reply.set(field::etag, etag_value);
   }
   if (options.last_modified) {
      reply.set(field::last_modified, modified_value);
   }
   if (!options.cache_control.empty()) {
      reply.set("Cache-Control", options.cache_control);
   }

   if (!range_value.satisfiable) {
      reply.result(status::range_not_satisfiable);
      reply.set(field::content_range, range_value.content_range);
      reply.set(field::content_length, "0");
      return stream_response::buffered(std::move(reply));
   }

   const auto first = range_value.bytes.first;
   const auto last = range_value.bytes.last;
   const auto length = metadata.size == 0 ? std::uint64_t{0} : last - first + 1U;
   if (range_value.partial) {
      reply.set(field::content_range, range_value.content_range);
   }
   reply.set(field::content_length, std::to_string(length));

   if (request_value.method() == method::head) {
      return stream_response::buffered(std::move(reply));
   }

   auto remaining = std::make_shared<std::uint64_t>(length);
   auto offset = std::make_shared<std::uint64_t>(first);
   const auto chunk_size = std::max<std::size_t>(1, options.chunk_bytes);
   return stream_response{
       .head = std::move(reply),
       .body = [descriptor, remaining, offset,
                chunk_size]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
          if (*remaining == 0) {
             co_return std::nullopt;
          }
          const auto bytes_to_read =
              static_cast<std::size_t>(std::min<std::uint64_t>(*remaining, static_cast<std::uint64_t>(chunk_size)));
          auto bytes = std::vector<std::byte>(bytes_to_read);
          const auto read = ::pread(*descriptor, bytes.data(), bytes.size(), static_cast<off_t>(*offset));
          if (read < 0) {
             FORGE_THROW_EXCEPTION(exceptions::internal, "failed to read opened HTTP file");
          }
          if (read == 0) {
             FORGE_THROW_EXCEPTION(exceptions::internal, "opened HTTP file ended before its advertised size");
          }
          const auto count = static_cast<std::size_t>(read);
          bytes.resize(count);
          *remaining -= count;
          *offset += count;
          co_return body_chunk{.bytes = std::move(bytes)};
       },
   };
}

} // namespace

boost::asio::awaitable<stream_response> file_response::materialize(const request& request_value) && {
   if (!server_path_) {
      if (!body_.valid()) {
         co_return stream_response::buffered(std::move(head_));
      }
      head_.version(request_value.version());
      head_.keep_alive(request_value.keep_alive());
      auto reader = std::move(body_);
      co_return stream_response{
          .head = std::move(head_),
          .body = [reader = std::move(reader)]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
             co_return co_await reader.async_read();
          },
      };
   }

   auto descriptor = std::shared_ptr<int>{};
   auto metadata = opened_file_metadata{};
   try {
      descriptor = open_file(path_, options_.symlinks);
      metadata = inspect_open_file(descriptor, options_);
   } catch (const exceptions::not_found&) {
      auto reply = make_text_response(request_value, status::not_found, "not found");
      if (request_value.method() == method::head) {
         reply.body().clear();
      }
      co_return stream_response::buffered(std::move(reply));
   }

   co_return make_file_stream(request_value, descriptor, metadata, options_);
}

boost::asio::awaitable<void> file_response::save_to(const std::filesystem::path& target) {
   auto output = std::ofstream{target, std::ios::binary | std::ios::trunc};
   if (!output) {
      throw_file_write_failed(target, "open");
   }
   while (auto chunk = co_await body_.async_read()) {
      output.write(reinterpret_cast<const char*>(chunk->bytes.data()),
                   static_cast<std::streamsize>(chunk->bytes.size()));
      if (!output) {
         throw_file_write_failed(target, "write");
      }
   }
   output.flush();
   if (!output) {
      throw_file_write_failed(target, "flush");
   }
   output.close();
   if (!output) {
      throw_file_write_failed(target, "close");
   }
}

static_file_root::static_file_root(std::filesystem::path root, file_options options) : options_(std::move(options)) {
   auto error = std::error_code{};
   root_ = std::filesystem::weakly_canonical(std::move(root), error);
   if (error) {
      throw exceptions::bad_request{"static file root must be a directory"};
   }
   const auto descriptor = ::open(root_.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
   if (descriptor < 0) {
      throw exceptions::bad_request{"static file root must be a directory"};
   }
   root_descriptor_ = own_descriptor(descriptor);
}

const std::filesystem::path& static_file_root::root() const noexcept {
   return root_;
}

boost::asio::awaitable<stream_response> static_file_root::serve(stream_request& request_value,
                                                                std::string_view relative_path) const {
   co_return co_await serve(request_value, relative_path, options_);
}

boost::asio::awaitable<stream_response>
static_file_root::serve(stream_request& request_value, std::string_view relative_path, file_options options) const {
   try {
      const auto descriptor = options.symlinks == symlink_policy::reject
                                  ? open_relative_file(root_descriptor_, relative_path)
                                  : open_followed_relative_file(root_, root_descriptor_, relative_path);
      const auto metadata = inspect_open_file(descriptor, options);
      co_return make_file_stream(request_value.context.request, descriptor, metadata, options);
   } catch (const exceptions::forbidden&) {
      co_return stream_response::buffered(
          make_text_response(request_value.context.request, status::forbidden, "forbidden"));
   } catch (const exceptions::not_found&) {
      co_return stream_response::buffered(
          make_text_response(request_value.context.request, status::not_found, "not found"));
   } catch (const exceptions::payload_too_large&) {
      co_return stream_response::buffered(
          make_text_response(request_value.context.request, status::payload_too_large, "file too large"));
   }
}

} // namespace forge::net::http

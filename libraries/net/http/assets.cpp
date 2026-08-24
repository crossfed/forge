module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cctype>
#include <coroutine>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.net.http.assets;

import forge.net.http.exceptions;
import forge.net.http.file;
import forge.net.http.stream;
import forge.net.http.target;
import forge.net.http.types;

namespace forge::net::http {
namespace {

[[nodiscard]] bool is_control(unsigned char value) noexcept {
   return value <= 0x1fU || value == 0x7fU;
}

[[nodiscard]] bool is_hex(unsigned char value) noexcept {
   return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] unsigned char decode_hex(unsigned char value) noexcept {
   if (value >= '0' && value <= '9') {
      return value - '0';
   }
   if (value >= 'A' && value <= 'F') {
      return value - 'A' + 10U;
   }
   return value - 'a' + 10U;
}

[[nodiscard]] std::string normalize_mount_path(std::string_view value) {
   if (value.empty() || value.front() != '/') {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "asset mount path must start with /");
   }
   if (value.find('?') != std::string_view::npos || value.find('#') != std::string_view::npos ||
       value.find('\\') != std::string_view::npos || value.find('%') != std::string_view::npos) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "asset mount path is malformed");
   }
   for (const auto character : value) {
      if (is_control(static_cast<unsigned char>(character))) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "asset mount path contains a control character");
      }
   }
   while (value.size() > 1U && value.back() == '/') {
      value.remove_suffix(1U);
   }
   auto start = std::size_t{1};
   while (start < value.size()) {
      const auto slash = value.find('/', start);
      const auto end = slash == std::string_view::npos ? value.size() : slash;
      const auto segment = value.substr(start, end - start);
      if (segment.empty() || segment == "." || segment == "..") {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "asset mount path is malformed");
      }
      if (slash == std::string_view::npos) {
         break;
      }
      start = slash + 1U;
   }
   return std::string{value};
}

void validate_relative_path(std::string_view value, std::string_view description) {
   if (value.empty() || value.front() == '/' || value.find('\\') != std::string_view::npos ||
       value.find('\0') != std::string_view::npos) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, std::string{description} + " is malformed");
   }
   auto start = std::size_t{0};
   while (start <= value.size()) {
      const auto slash = value.find('/', start);
      const auto end = slash == std::string_view::npos ? value.size() : slash;
      const auto segment = value.substr(start, end - start);
      if (segment.empty() || segment == "." || segment == "..") {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, std::string{description} + " is malformed");
      }
      if (slash == std::string_view::npos) {
         break;
      }
      start = slash + 1U;
   }
}

[[nodiscard]] std::string decode_relative_path(std::string_view value, std::uint64_t maximum) {
   if (value.size() > maximum) {
      throw exceptions::forbidden{"asset request path exceeds the configured limit"};
   }
   auto result = std::string{};
   result.reserve(value.size());
   for (auto index = std::size_t{0}; index != value.size(); ++index) {
      auto character = static_cast<unsigned char>(value[index]);
      auto percent_escaped = false;
      if (character == '%') {
         if (index + 2U >= value.size() || !is_hex(static_cast<unsigned char>(value[index + 1U])) ||
             !is_hex(static_cast<unsigned char>(value[index + 2U]))) {
            throw exceptions::forbidden{"asset request path has an invalid percent escape"};
         }
         character = static_cast<unsigned char>((decode_hex(static_cast<unsigned char>(value[index + 1U])) << 4U) |
                                                decode_hex(static_cast<unsigned char>(value[index + 2U])));
         index += 2U;
         percent_escaped = true;
      }
      if (is_control(character) || character == '\\' || (percent_escaped && character == '/')) {
         throw exceptions::forbidden{"asset request path is unsafe"};
      }
      if (character == '/' && result.empty()) {
         throw exceptions::forbidden{"asset request path is unsafe"};
      }
      result.push_back(static_cast<char>(character));
      if (result.size() > maximum) {
         throw exceptions::forbidden{"asset request path exceeds the configured limit"};
      }
   }
   validate_relative_path(result, "asset request path");
   return result;
}

[[nodiscard]] std::string raw_path(std::string_view target_value) {
   const auto query = target_value.find('?');
   return std::string{target_value.substr(0, query)};
}

[[nodiscard]] std::string_view relative_raw_path(std::string_view path, std::string_view prefix) {
   if (prefix == "/") {
      return path.size() > 1U ? path.substr(1U) : std::string_view{};
   }
   if (path == prefix) {
      return {};
   }
   if (!path.starts_with(prefix) || path.size() <= prefix.size() || path[prefix.size()] != '/') {
      return {};
   }
   return path.substr(prefix.size() + 1U);
}

[[nodiscard]] std::string lowercase(std::string_view value) {
   auto result = std::string{value};
   std::ranges::transform(result, result.begin(),
                          [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
   return result;
}

[[nodiscard]] std::optional<std::string_view> content_type_for(std::string_view path) {
   const auto dot = path.rfind('.');
   if (dot == std::string_view::npos || dot + 1U == path.size()) {
      return std::nullopt;
   }
   const auto extension = lowercase(path.substr(dot + 1U));
   if (extension == "html") {
      return "text/html; charset=utf-8";
   }
   if (extension == "css") {
      return "text/css; charset=utf-8";
   }
   if (extension == "js" || extension == "mjs") {
      return "text/javascript; charset=utf-8";
   }
   if (extension == "json" || extension == "map") {
      return "application/json";
   }
   if (extension == "svg") {
      return "image/svg+xml";
   }
   if (extension == "png") {
      return "image/png";
   }
   if (extension == "jpg" || extension == "jpeg") {
      return "image/jpeg";
   }
   if (extension == "webp") {
      return "image/webp";
   }
   if (extension == "ico") {
      return "image/x-icon";
   }
   if (extension == "txt") {
      return "text/plain; charset=utf-8";
   }
   if (extension == "wasm") {
      return "application/wasm";
   }
   if (extension == "woff2") {
      return "font/woff2";
   }
   return std::nullopt;
}

[[nodiscard]] bool fingerprinted(std::string_view path) {
   const auto slash = path.rfind('/');
   const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1U);
   const auto extension = name.rfind('.');
   if (extension == std::string_view::npos) {
      return false;
   }
   auto prefix = name.substr(0, extension);
   while (!prefix.empty()) {
      const auto dot = prefix.rfind('.');
      const auto token = dot == std::string_view::npos ? prefix : prefix.substr(dot + 1U);
      if (token.size() >= 5U && std::ranges::all_of(token, [](unsigned char value) { return is_hex(value); })) {
         return true;
      }
      if (dot == std::string_view::npos) {
         break;
      }
      prefix = prefix.substr(0, dot);
   }
   return false;
}

[[nodiscard]] bool spa_candidate(std::string_view path) {
   const auto slash = path.rfind('/');
   const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1U);
   return !name.empty() && name.find('.') == std::string_view::npos;
}

[[nodiscard]] file_options options_for(std::string_view path, const asset_mount& mount) {
   const auto content_type = content_type_for(path);
   if (!content_type.has_value()) {
      throw exceptions::not_found{"asset type is not allow-listed"};
   }
   return file_options{
       .content_type = std::string{*content_type},
       .symlinks = symlink_policy::reject,
       .max_file_bytes = mount.max_file_bytes,
       .cache_control = path == mount.index   ? "no-cache"
                        : fingerprinted(path) ? "public, max-age=31536000, immutable"
                                              : "no-cache",
   };
}

} // namespace

struct asset_bundle::impl {
   impl(asset_mount value, forge::asio::compute::executor read_executor)
       : mount(std::move(value)), prefix(normalize_mount_path(mount.path)),
         root(std::make_shared<static_file_root>(mount.root, std::move(read_executor))) {
      mount.path = prefix;
      if (mount.max_file_bytes == 0U || mount.max_file_bytes > 1024ULL * 1024ULL * 1024ULL) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "asset max_file_bytes must be between 1 and 1073741824");
      }
      if (mount.max_path_bytes == 0U || mount.max_path_bytes > 16ULL * 1024ULL) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "asset max_path_bytes must be between 1 and 16384");
      }
      validate_relative_path(mount.index, "asset index");
      static_cast<void>(options_for(mount.index, mount));
   }

   asset_mount mount;
   std::string prefix;
   std::shared_ptr<static_file_root> root;
};

asset_bundle::asset_bundle(asset_mount value, forge::asio::compute::executor read_executor)
    : impl_(std::make_shared<impl>(std::move(value), std::move(read_executor))) {}

asset_bundle::~asset_bundle() = default;

std::string_view asset_bundle::path() const noexcept {
   return impl_->prefix;
}

bool asset_bundle::contains(std::string_view request_path) const noexcept {
   if (impl_->prefix == "/") {
      return request_path.starts_with('/');
   }
   return request_path == impl_->prefix ||
          (request_path.size() > impl_->prefix.size() && request_path.starts_with(impl_->prefix) &&
           request_path[impl_->prefix.size()] == '/');
}

bool asset_bundle::serves(method request_method, std::string_view request_path) const noexcept {
   return (request_method == method::get || request_method == method::head) && contains(request_path);
}

boost::asio::awaitable<stream_response> asset_bundle::serve(stream_request& request_value) const {
   const auto raw = raw_path(request_value.context.parsed_target.original);
   if (!contains(raw)) {
      throw exceptions::forbidden{"asset request path does not match the mount"};
   }
   const auto relative = relative_raw_path(raw, impl_->prefix);
   auto selected = relative.empty() ? impl_->mount.index : decode_relative_path(relative, impl_->mount.max_path_bytes);
   if (!content_type_for(selected).has_value() && impl_->mount.spa_fallback && !relative.empty() &&
       spa_candidate(selected)) {
      selected = impl_->mount.index;
   }
   auto options = options_for(selected, impl_->mount);
   auto result = co_await impl_->root->serve(request_value, selected, options);
   if (result.head.result() == status::not_found && impl_->mount.spa_fallback && !relative.empty() &&
       spa_candidate(selected)) {
      selected = impl_->mount.index;
      options = options_for(selected, impl_->mount);
      result = co_await impl_->root->serve(request_value, selected, std::move(options));
   }
   co_return result;
}

} // namespace forge::net::http

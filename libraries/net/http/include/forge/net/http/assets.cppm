module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

export module forge.net.http.assets;

import forge.net.http.stream;
import forge.net.http.types;

export namespace forge::net::http {

struct asset_mount {
   std::string path = "/admin";
   std::filesystem::path root;
   std::string index = "index.html";
   bool spa_fallback = true;
   std::uint64_t max_file_bytes = 16 * 1024 * 1024;
   std::uint64_t max_path_bytes = 4 * 1024;
};

class asset_bundle {
 public:
   explicit asset_bundle(asset_mount value);
   ~asset_bundle();

   asset_bundle(const asset_bundle&) = default;
   asset_bundle(asset_bundle&&) noexcept = default;
   asset_bundle& operator=(const asset_bundle&) = default;
   asset_bundle& operator=(asset_bundle&&) noexcept = default;

   [[nodiscard]] std::string_view path() const noexcept;
   [[nodiscard]] bool contains(std::string_view request_path) const noexcept;
   [[nodiscard]] bool serves(method request_method, std::string_view request_path) const noexcept;
   [[nodiscard]] boost::asio::awaitable<stream_response> serve(stream_request& request_value) const;

 private:
   struct impl;

   std::shared_ptr<impl> impl_;
};

} // namespace forge::net::http

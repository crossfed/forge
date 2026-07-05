#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

import forge.db.driver;
import forge.db.record;

class package_session final : public forge::db::session {
 public:
   [[nodiscard]] forge::db::capabilities capabilities() const noexcept override {
      return forge::db::capabilities{.snapshot_reads = true, .writes = true};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::family, forge::db::record_key) override {
      co_return std::nullopt;
   }

   boost::asio::awaitable<void> put(forge::db::family, forge::db::record_key, std::vector<std::byte>) override {
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::family, forge::db::record_key) override {
      co_return;
   }

   boost::asio::awaitable<forge::db::record_page> scan_page(forge::db::family,
                                                            forge::db::record_range,
                                                            forge::db::page_request) override {
      co_return forge::db::record_page{};
   }

   boost::asio::awaitable<void> commit() override {
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      co_return;
   }
};

int main() {
   static_assert(std::derived_from<package_session, forge::db::session>);
   auto family = forge::db::family{"package"};
   return family.name == "package" ? 0 : 1;
}

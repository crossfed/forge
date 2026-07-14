#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace forge::db::core {

class participant_access_impl final : public participant_access {
 public:
   explicit participant_access_impl(session& active) noexcept;

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, record_key key) override;

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(family column_family, record_key key) override;

   boost::asio::awaitable<void>
   put(family column_family, record_key key, std::vector<std::byte> value) override;

   boost::asio::awaitable<void>
   erase(family column_family, record_key key) override;

   boost::asio::awaitable<record_page>
   scan_page(family column_family, record_range range, page_request request) override;

 private:
   session* active_;
};

} // namespace forge::db::core

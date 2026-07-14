module;

#include <boost/asio/awaitable.hpp>

#include <optional>
#include <utility>
#include <vector>

module forge.db.core.driver;

#include "details/participant_access_impl.hxx"

namespace forge::db::core {

participant_access_impl::participant_access_impl(session& active) noexcept : active_{&active} {}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
participant_access_impl::get(family column_family, record_key key) {
   co_return co_await active_->get(std::move(column_family), std::move(key));
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
participant_access_impl::get_for_update(family column_family, record_key key) {
   co_return co_await active_->get_for_update(std::move(column_family), std::move(key));
}

boost::asio::awaitable<void>
participant_access_impl::put(family column_family, record_key key, std::vector<std::byte> value) {
   co_await active_->put(std::move(column_family), std::move(key), std::move(value));
}

boost::asio::awaitable<void>
participant_access_impl::erase(family column_family, record_key key) {
   co_await active_->erase(std::move(column_family), std::move(key));
}

boost::asio::awaitable<record_page>
participant_access_impl::scan_page(family column_family, record_range range, page_request request) {
   co_return co_await active_->scan_page(std::move(column_family), std::move(range), std::move(request));
}

} // namespace forge::db::core

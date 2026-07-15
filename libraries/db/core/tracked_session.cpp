module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

module forge.db.core.driver;

#include "details/tracked_session.hxx"

namespace forge::db::core::detail {

tracked_session::tracked_session(std::unique_ptr<session> inner,
                                 driver_state::open_admission admission)
    : inner_{std::move(inner)}, state_{admission.publish()} {}

tracked_session::~tracked_session() {
   inner_.reset();
   state_->release_session();
}

forge::db::core::capabilities tracked_session::capabilities() const noexcept {
   return inner_->capabilities();
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
tracked_session::get(family column_family, record_key key) {
   co_return co_await inner_->get(std::move(column_family), std::move(key));
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
tracked_session::get_for_update(family column_family, record_key key) {
   co_return co_await inner_->get_for_update(std::move(column_family), std::move(key));
}

boost::asio::awaitable<void> tracked_session::put(family column_family,
                                                  record_key key,
                                                  std::vector<std::byte> value) {
   co_await inner_->put(std::move(column_family), std::move(key), std::move(value));
}

boost::asio::awaitable<void> tracked_session::erase(family column_family, record_key key) {
   co_await inner_->erase(std::move(column_family), std::move(key));
}

boost::asio::awaitable<record_page> tracked_session::scan_page(family column_family,
                                                               record_range range,
                                                               page_request request) {
   co_return co_await inner_->scan_page(
      std::move(column_family), std::move(range), std::move(request));
}

boost::asio::awaitable<void> tracked_session::create_savepoint() {
   co_await inner_->create_savepoint();
}

boost::asio::awaitable<void> tracked_session::rollback_to_savepoint() {
   co_await inner_->rollback_to_savepoint();
}

boost::asio::awaitable<void> tracked_session::release_savepoint() {
   co_await inner_->release_savepoint();
}

boost::asio::awaitable<void> tracked_session::commit() {
   co_await inner_->commit();
}

boost::asio::awaitable<void> tracked_session::rollback() {
   co_await inner_->rollback();
}

} // namespace forge::db::core::detail

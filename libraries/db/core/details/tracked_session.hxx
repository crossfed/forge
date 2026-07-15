#pragma once

#include "driver_state.hxx"

namespace forge::db::core::detail {

class tracked_session final : public session {
 public:
   tracked_session(std::unique_ptr<session> inner, driver_state::open_admission admission);
   ~tracked_session() override;

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(family column_family,
                                                                     record_key key) override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(family column_family, record_key key) override;
   boost::asio::awaitable<void> put(family column_family,
                                    record_key key,
                                    std::vector<std::byte> value) override;
   boost::asio::awaitable<void> erase(family column_family, record_key key) override;
   boost::asio::awaitable<record_page> scan_page(family column_family,
                                                 record_range range,
                                                 page_request request) override;
   boost::asio::awaitable<void> create_savepoint() override;
   boost::asio::awaitable<void> rollback_to_savepoint() override;
   boost::asio::awaitable<void> release_savepoint() override;
   boost::asio::awaitable<void> commit() override;
   boost::asio::awaitable<void> rollback() override;

 private:
   std::unique_ptr<session> inner_;
   std::shared_ptr<driver_state> state_;
};

} // namespace forge::db::core::detail

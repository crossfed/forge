#pragma once

namespace forge::db::mdbx::detail {

class driver_impl;

class transaction_session final : public forge::db::core::session {
 public:
   transaction_session(std::shared_ptr<driver_impl> owner,
                       forge::asio::gate::ticket ticket,
                       MDBX_txn* transaction);
   ~transaction_session() override;

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(forge::db::core::family column_family,
       forge::db::core::record_key key) override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(forge::db::core::family column_family,
                  forge::db::core::record_key key) override;
   boost::asio::awaitable<void>
   put(forge::db::core::family column_family,
       forge::db::core::record_key key,
       std::vector<std::byte> value) override;
   boost::asio::awaitable<void>
   erase(forge::db::core::family column_family,
         forge::db::core::record_key key) override;
   boost::asio::awaitable<forge::db::core::record_page>
   scan_page(forge::db::core::family column_family,
             forge::db::core::record_range range,
             forge::db::core::page_request request) override;
   boost::asio::awaitable<void> create_savepoint() override;
   boost::asio::awaitable<void> rollback_to_savepoint() override;
   boost::asio::awaitable<void> release_savepoint() override;
   boost::asio::awaitable<void> commit() override;
   boost::asio::awaitable<void> rollback() override;

 private:
   [[nodiscard]] MDBX_txn* active() const;
   void require_active() const;
   void finish() noexcept;

   std::shared_ptr<driver_impl> owner_;
   forge::asio::gate::ticket ticket_;
   std::vector<MDBX_txn*> transactions_;
};

} // namespace forge::db::mdbx::detail

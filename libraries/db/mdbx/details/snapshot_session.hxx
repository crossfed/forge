#pragma once

namespace forge::db::mdbx::detail {

class driver_impl;

class snapshot_session final : public forge::db::core::session {
 public:
   snapshot_session(std::shared_ptr<driver_impl> owner, MDBX_txn* anchor);
   ~snapshot_session() override;

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(forge::db::core::family column_family,
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
   boost::asio::awaitable<void> commit() override;
   boost::asio::awaitable<void> rollback() override;

 private:
   class clone final {
    public:
      clone(snapshot_session& owner, MDBX_txn* transaction);
      ~clone();

      clone(const clone&) = delete;
      clone& operator=(const clone&) = delete;

      [[nodiscard]] MDBX_txn* native() const noexcept;

    private:
      snapshot_session& owner_;
      MDBX_txn* transaction_;
   };

   [[nodiscard]] clone acquire_clone();
   void recycle(MDBX_txn* transaction) noexcept;

   std::shared_ptr<driver_impl> owner_;
   MDBX_txn* anchor_ = nullptr;
   std::mutex clones_mutex_;
   std::vector<MDBX_txn*> clones_;
};

} // namespace forge::db::mdbx::detail

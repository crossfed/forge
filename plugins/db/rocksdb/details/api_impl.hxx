#pragma once

namespace forge::plugins::db::rocksdb {

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<impl> owner);

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, std::vector<std::byte> key, read_options options) override;
   boost::asio::awaitable<void>
   put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value, write_options options) override;
   boost::asio::awaitable<void> erase(family column_family, std::vector<std::byte> key, write_options options) override;
   boost::asio::awaitable<void> write(std::vector<operation> operations, write_options options) override;
   boost::asio::awaitable<std::vector<entry>>
   scan(family column_family, std::vector<std::byte> prefix, read_options options) override;
   boost::asio::awaitable<scan_result> scan_page(family column_family, scan_request request) override;
   boost::asio::awaitable<std::shared_ptr<transaction>> begin(write_options options) override;
   boost::asio::awaitable<void> flush_wal(bool sync) override;

 private:
   std::shared_ptr<impl> owner_;
};

} // namespace forge::plugins::db::rocksdb

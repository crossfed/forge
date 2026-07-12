#pragma once

namespace forge::plugins::db::store {

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<impl> owner);

   boost::asio::awaitable<void>
   add_store(std::string name,
             std::shared_ptr<forge::db::core::driver> driver,
             store_options options) override;
   boost::asio::awaitable<store_handle> store(std::string name) override;
   boost::asio::awaitable<void> flush(std::string name, bool sync) override;
   boost::asio::awaitable<void> flush_all(bool sync) override;
   boost::asio::awaitable<::forge::plugins::db::store::status> status() override;

 private:
   std::shared_ptr<impl> owner_;
};

} // namespace forge::plugins::db::store

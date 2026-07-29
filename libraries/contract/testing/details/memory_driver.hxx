#pragma once

#include <memory>

namespace forge::contract::testing {

class memory_driver final : public forge::db::core::driver {
 public:
   struct state;

   memory_driver();
   ~memory_driver() override;

   boost::asio::awaitable<void> async_flush(bool sync) override;
   [[nodiscard]] std::vector<std::uint8_t> snapshot() const;

 private:
   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_transaction() override;
   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_snapshot() override;

   std::shared_ptr<state> state_;
};

} // namespace forge::contract::testing

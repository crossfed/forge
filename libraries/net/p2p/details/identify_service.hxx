#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/system/error_code.hpp>

namespace forge::net::p2p::detail {

class identify_service {
 public:
   struct outcome {
      identify::state state = identify::state::failed;
      std::optional<identify::document> document;
      std::string error;
   };

   using operation = std::function<boost::asio::awaitable<identify::document>()>;

   explicit identify_service(boost::asio::any_io_executor executor);

   boost::asio::awaitable<outcome> async_identify(std::uint64_t session_id, operation run);
   [[nodiscard]] std::size_t retained() const noexcept;
   void forget(std::uint64_t session_id) noexcept;
   void close() noexcept;

 private:
   using completion_channel = boost::asio::experimental::concurrent_channel<void(boost::system::error_code, outcome)>;

   struct attempt {
      bool running = true;
      bool completed = false;
      outcome result;
      std::vector<std::weak_ptr<completion_channel>> waiters;
   };

   void complete(const std::shared_ptr<attempt>& current, outcome result) noexcept;

   boost::asio::any_io_executor executor_;
   mutable std::mutex mutex_;
   std::map<std::uint64_t, std::shared_ptr<attempt>> attempts_;
   bool closed_ = false;
};

} // namespace forge::net::p2p::detail

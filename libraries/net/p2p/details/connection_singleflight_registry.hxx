#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace forge::net::p2p::detail {

class connection_singleflight_registry {
 private:
   struct entry;

 public:
   struct outcome {
      bool succeeded = false;
      std::optional<exceptions::code> error;
      std::string message;
   };

   class lease {
    public:
      lease() = default;
      lease(const lease&) = delete;
      lease& operator=(const lease&) = delete;
      lease(lease&&) noexcept = default;
      lease& operator=(lease&&) noexcept = default;

      [[nodiscard]] bool leader() const noexcept;
      boost::asio::awaitable<outcome> wait();

    private:
      using completion_channel =
          boost::asio::experimental::concurrent_channel<void(boost::system::error_code, outcome)>;

      lease(peer_id peer, std::shared_ptr<entry> owner, std::shared_ptr<completion_channel> completion, bool leader);

      peer_id peer_;
      std::shared_ptr<entry> owner_;
      std::shared_ptr<completion_channel> completion_;
      bool leader_ = false;

      friend class connection_singleflight_registry;
   };

   [[nodiscard]] lease join(const peer_id& peer, boost::asio::any_io_executor executor);
   void succeed(lease& participant) noexcept;
   void fail(lease& participant, exceptions::code error, std::string message) noexcept;
   void leave(lease& participant) noexcept;
   void close() noexcept;
   [[nodiscard]] std::size_t size() const noexcept;

 private:
   struct entry {
      outcome result;
      bool completed = false;
      std::size_t participants = 0;
      std::vector<std::weak_ptr<lease::completion_channel>> completions;
   };

   void complete(entry& owner, outcome result) noexcept;

   std::map<peer_id, std::shared_ptr<entry>> entries_;
};

} // namespace forge::net::p2p::detail

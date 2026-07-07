module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

export module forge.db.core.driver;

import forge.db.core.record;

export namespace forge::db::core {

struct capabilities {
   bool snapshot_reads = false;
   bool writes = true;
};

class session {
 public:
   virtual ~session() = default;

   [[nodiscard]] virtual capabilities capabilities() const noexcept = 0;
   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(family column_family,
                                                                             record_key key) = 0;
   virtual boost::asio::awaitable<void> put(family column_family, record_key key, std::vector<std::byte> value) = 0;
   virtual boost::asio::awaitable<void> erase(family column_family, record_key key) = 0;
   virtual boost::asio::awaitable<record_page> scan_page(family column_family,
                                                         record_range range,
                                                         page_request request) = 0;
   virtual boost::asio::awaitable<void> commit() = 0;
   virtual boost::asio::awaitable<void> rollback() = 0;
};

class transaction {
 public:
   using after_commit_fn = std::function<boost::asio::awaitable<void>()>;
   using after_rollback_fn = std::function<void()>;

   transaction() = default;
   transaction(std::unique_ptr<session> active, boost::asio::any_io_executor cleanup_executor);
   ~transaction();

   transaction(const transaction&) = delete;
   transaction& operator=(const transaction&) = delete;

   transaction(transaction&&) noexcept;
   transaction& operator=(transaction&&) noexcept;

   [[nodiscard]] bool active() const noexcept;

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(family column_family, record_key key);
   boost::asio::awaitable<void> put(family column_family, record_key key, std::vector<std::byte> value);
   boost::asio::awaitable<void> erase(family column_family, record_key key);
   boost::asio::awaitable<record_page> scan_page(family column_family, record_range range, page_request request);

   void after_commit(after_commit_fn hook);
   void after_rollback(after_rollback_fn hook);

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

class snapshot {
 public:
   snapshot() = default;
   explicit snapshot(std::unique_ptr<session> active);

   [[nodiscard]] bool active() const noexcept;

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(family column_family, record_key key);
   boost::asio::awaitable<record_page> scan_page(family column_family, record_range range, page_request request);

 private:
   std::shared_ptr<session> active_;
};

class driver {
 public:
   virtual ~driver() = default;

   boost::asio::awaitable<transaction> begin_transaction();
   boost::asio::awaitable<snapshot> begin_read();
   virtual boost::asio::awaitable<void> async_flush(bool sync) = 0;

 private:
   virtual boost::asio::awaitable<std::unique_ptr<session>> open_transaction() = 0;
   virtual boost::asio::awaitable<std::unique_ptr<session>> open_snapshot() = 0;
};

} // namespace forge::db::core

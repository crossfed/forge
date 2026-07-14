module;

#include <boost/asio/awaitable.hpp>

#include <optional>

export module forge.db.revision.transaction;

import forge.db.core.driver;
import forge.db.revision.types;

export namespace forge::db::revision {

class scope {
 public:
   scope() = default;
   explicit scope(revision_id_t candidate);

   [[nodiscard]] revision_id_t id() const;

 private:
   std::optional<revision_id_t> candidate_;
};

class transaction {
 public:
   transaction() = default;
   ~transaction() = default;

   transaction(const transaction&) = delete;
   transaction& operator=(const transaction&) = delete;
   transaction(transaction&&) noexcept = default;
   transaction& operator=(transaction&&) noexcept = default;
   transaction(forge::db::core::transaction active, scope joined);

   [[nodiscard]] forge::db::core::transaction& db_transaction();
   [[nodiscard]] revision_id_t id() const;

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   std::optional<forge::db::core::transaction> active_;
   scope scope_;
};

} // namespace forge::db::revision

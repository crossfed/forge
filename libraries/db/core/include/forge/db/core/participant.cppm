module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module forge.db.core.participant;

import forge.db.core.record;

export namespace forge::db::core {

using savepoint_id_t = std::uint64_t;

enum class mutation_kind : std::uint8_t {
   put,
   erase,
};

enum class mutation_policy : std::uint8_t {
   inherit,
   reversible,
   excluded,
   forbidden,
   forbidden_when_captured,
};

struct record_address {
   family column_family;
   record_key key;
};

struct record_lock_claim {
   family column_family;
   record_key key;
};

struct record_mutation {
   mutation_kind kind = mutation_kind::put;
   family column_family;
   record_key key;
   std::optional<std::vector<std::byte>> before;
   std::optional<std::vector<std::byte>> after;
   std::optional<record_address> retention_guard;
};

class participant_access {
 public:
   virtual ~participant_access() = default;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, record_key key) = 0;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(family column_family, record_key key) = 0;

   virtual boost::asio::awaitable<void>
   put(family column_family, record_key key, std::vector<std::byte> value) = 0;

   virtual boost::asio::awaitable<void>
   erase(family column_family, record_key key) = 0;

   virtual boost::asio::awaitable<record_page>
   scan_page(family column_family, record_range range, page_request request) = 0;
};

class transaction_participant {
 public:
   virtual ~transaction_participant() = default;

   [[nodiscard]] virtual std::string_view name() const noexcept = 0;

   [[nodiscard]] virtual std::span<const family> exclusive_families() const noexcept {
      return {};
   }

   [[nodiscard]] virtual std::span<const record_lock_claim> prewrite_locks() const noexcept {
      return {};
   }

   [[nodiscard]] virtual mutation_policy
   classify(const family&, const record_key&, mutation_kind) const noexcept {
      return mutation_policy::inherit;
   }

   [[nodiscard]] virtual bool captures_mutations() const noexcept {
      return false;
   }

   [[nodiscard]] virtual std::optional<std::vector<std::byte>>
   retention_token(const record_mutation&) const {
      return std::nullopt;
   }

   [[nodiscard]] virtual std::optional<record_address>
   make_retention_guard(const record_mutation&, std::span<const std::byte>) const {
      return std::nullopt;
   }

   virtual boost::asio::awaitable<void> prepare_mutation(const record_mutation&) {
      co_return;
   }

   virtual void publish_mutation() noexcept {}
   virtual void discard_mutation() noexcept {}

   virtual boost::asio::awaitable<void> prepare_savepoint(savepoint_id_t) {
      co_return;
   }

   virtual void publish_savepoint(savepoint_id_t) noexcept {}
   virtual void discard_savepoint(savepoint_id_t) noexcept {}

   virtual boost::asio::awaitable<void>
   rollback_to_savepoint(savepoint_id_t, participant_access&) {
      co_return;
   }

   virtual boost::asio::awaitable<void>
   release_savepoint(savepoint_id_t, participant_access&) {
      co_return;
   }

   virtual boost::asio::awaitable<void> prepare_commit(participant_access&) {
      co_return;
   }
};

} // namespace forge::db::core

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge::db::revision::detail {

class transaction_impl final : public forge::db::core::transaction_participant {
 public:
   transaction_impl(forge::db::core::family family, state initial);

   [[nodiscard]] revision_id_t id() const noexcept;
   [[nodiscard]] std::string_view name() const noexcept override;
   [[nodiscard]] bool captures_mutations() const noexcept override;
   [[nodiscard]] std::optional<std::vector<std::byte>>
   retention_token(const forge::db::core::record_mutation& mutation) const override;

   boost::asio::awaitable<void>
   prepare_mutation(const forge::db::core::record_mutation& mutation) override;
   void publish_mutation() noexcept override;
   void discard_mutation() noexcept override;

   boost::asio::awaitable<void> prepare_savepoint(forge::db::core::savepoint_id_t id) override;
   void publish_savepoint(forge::db::core::savepoint_id_t id) noexcept override;
   void discard_savepoint(forge::db::core::savepoint_id_t id) noexcept override;
   boost::asio::awaitable<void>
   rollback_to_savepoint(forge::db::core::savepoint_id_t id,
                         forge::db::core::participant_access& access) override;
   boost::asio::awaitable<void>
   release_savepoint(forge::db::core::savepoint_id_t id,
                     forge::db::core::participant_access& access) override;

   boost::asio::awaitable<void> prepare_commit(forge::db::core::participant_access& access) override;

 private:
   struct address {
      std::string family;
      std::vector<std::byte> key;

      auto operator<=>(const address&) const = default;
   };

   struct captured_delta {
      address location;
      std::optional<std::vector<std::byte>> before;
      std::optional<std::vector<std::byte>> after;
      std::optional<forge::db::core::record_address> retention_guard;
   };

   struct savepoint_frame {
      forge::db::core::savepoint_id_t id = 0;
      std::size_t delta_count = 0;
      std::map<std::size_t, std::optional<std::vector<std::byte>>> previous_after;
      std::map<std::size_t, std::optional<forge::db::core::record_address>> previous_guards;
   };

   void rebuild_index();

   forge::db::core::family family_;
   state state_;
   revision_id_t candidate_ = 0;
   std::vector<captured_delta> deltas_;
   std::map<address, std::size_t> index_;
   std::optional<forge::db::core::record_mutation> pending_mutation_;
   std::optional<savepoint_frame> pending_savepoint_;
   std::vector<savepoint_frame> savepoints_;
   bool prepared_ = false;
};

} // namespace forge::db::revision::detail

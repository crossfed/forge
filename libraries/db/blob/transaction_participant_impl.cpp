module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

module forge.db.blob.transaction;

#include "details/transaction_participant_impl.hxx"
#include "details/key_codec.hxx"

namespace forge::db::blob::detail {

transaction_participant_impl::transaction_participant_impl(forge::db::core::family data,
                                                           forge::db::core::family refs) noexcept
    : data_{std::move(data)}, refs_{std::move(refs)} {}

std::string_view transaction_participant_impl::name() const noexcept {
   return "forge.db.blob";
}

forge::db::core::mutation_policy
transaction_participant_impl::classify(const forge::db::core::family& family,
                                       const forge::db::core::record_key& key,
                                       forge::db::core::mutation_kind kind) const noexcept {
   if (key.empty()) {
      return forge::db::core::mutation_policy::inherit;
   }

   const auto prefix = std::to_integer<std::uint8_t>(key.bytes().front());
   if (family.name == data_.name && prefix == 0x10U) {
      return kind == forge::db::core::mutation_kind::erase
                ? forge::db::core::mutation_policy::forbidden
                : forge::db::core::mutation_policy::excluded;
   }
   if (family.name == refs_.name && prefix == 0x20U) {
      return forge::db::core::mutation_policy::reversible;
   }
   if (family.name == refs_.name && prefix == 0x30U) {
      return forge::db::core::mutation_policy::excluded;
   }
   return forge::db::core::mutation_policy::inherit;
}

std::optional<forge::db::core::record_address>
transaction_participant_impl::make_retention_guard(
   const forge::db::core::record_mutation& mutation,
   std::span<const std::byte> token) const {
   if (mutation.column_family.name != refs_.name ||
       mutation.kind != forge::db::core::mutation_kind::erase ||
       !mutation.before.has_value()) {
      return std::nullopt;
   }
   auto key = retention_barrier_key(mutation.key, token);
   if (!key) {
      return std::nullopt;
   }
   return forge::db::core::record_address{.column_family = refs_, .key = std::move(*key)};
}

} // namespace forge::db::blob::detail

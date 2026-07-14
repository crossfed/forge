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

namespace {

void append_name_segment(std::string& result, std::string_view value) {
   result.append(std::to_string(value.size()));
   result.push_back(':');
   result.append(value);
}

std::string participant_name(const forge::db::core::family& data,
                             const forge::db::core::family& refs) {
   auto result = std::string{"forge.db.blob:"};
   append_name_segment(result, data.name);
   result.push_back(':');
   append_name_segment(result, refs.name);
   return result;
}

} // namespace

transaction_participant_impl::transaction_participant_impl(forge::db::core::family data,
                                                           forge::db::core::family refs)
    : name_{participant_name(data, refs)}, families_{std::move(data), std::move(refs)} {}

std::string_view transaction_participant_impl::name() const noexcept {
   return name_;
}

std::span<const forge::db::core::family>
transaction_participant_impl::exclusive_families() const noexcept {
   return families_;
}

forge::db::core::mutation_policy
transaction_participant_impl::classify(const forge::db::core::family& family,
                                       const forge::db::core::record_key& key,
                                       forge::db::core::mutation_kind kind) const noexcept {
   if (key.empty()) {
      return forge::db::core::mutation_policy::inherit;
   }

   const auto prefix = std::to_integer<std::uint8_t>(key.bytes().front());
   if (family.name == families_[0].name && prefix == 0x10U) {
      return kind == forge::db::core::mutation_kind::erase
                ? forge::db::core::mutation_policy::forbidden_when_captured
                : forge::db::core::mutation_policy::excluded;
   }
   if (family.name == families_[1].name && prefix == 0x20U) {
      return forge::db::core::mutation_policy::reversible;
   }
   if (family.name == families_[1].name && prefix == 0x30U) {
      return forge::db::core::mutation_policy::excluded;
   }
   return forge::db::core::mutation_policy::inherit;
}

std::optional<forge::db::core::record_address>
transaction_participant_impl::make_retention_guard(
   const forge::db::core::record_mutation& mutation,
   std::span<const std::byte> token) const {
   if (mutation.column_family.name != families_[1].name ||
       mutation.kind != forge::db::core::mutation_kind::erase ||
       !mutation.before.has_value()) {
      return std::nullopt;
   }
   auto key = retention_barrier_key(mutation.key, token);
   if (!key) {
      return std::nullopt;
   }
   return forge::db::core::record_address{.column_family = families_[1], .key = std::move(*key)};
}

} // namespace forge::db::blob::detail

#pragma once

#include <string_view>
#include <span>
#include <optional>

namespace forge::db::blob::detail {

class transaction_participant_impl final : public forge::db::core::transaction_participant {
 public:
   transaction_participant_impl(forge::db::core::family data, forge::db::core::family refs) noexcept;

   [[nodiscard]] std::string_view name() const noexcept override;
   [[nodiscard]] forge::db::core::mutation_policy
   classify(const forge::db::core::family& family,
            const forge::db::core::record_key& key,
            forge::db::core::mutation_kind kind) const noexcept override;
   [[nodiscard]] std::optional<forge::db::core::record_address>
   make_retention_guard(const forge::db::core::record_mutation& mutation,
                        std::span<const std::byte> token) const override;

 private:
   forge::db::core::family data_;
   forge::db::core::family refs_;
};

} // namespace forge::db::blob::detail

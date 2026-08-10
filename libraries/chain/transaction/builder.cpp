module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

module forge.chain.transaction.builder;

import forge.chain.protocol.block;
import forge.raw.raw;

namespace forge::chain::transaction {

namespace {

void validate(const context& context, const options& options) {
   if (context.chain.empty() || context.reference_block.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_context, "chain and reference block must be set");
   }
   if (!options.expiration && options.expiration_seconds == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "transaction expiration must be greater than zero");
   }
   if (!options.expiration && context.reference_time.sec_since_epoch() >
       std::numeric_limits<std::uint32_t>::max() - options.expiration_seconds) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "transaction expiration exceeds the wire time range");
   }
   if (options.expiration && options.expiration->sec_since_epoch() <= context.reference_time.sec_since_epoch()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "transaction expiration must follow the reference time");
   }
}

} // namespace

std::uint16_t reference_block_num(const chain::protocol::block_id& value) noexcept {
   return static_cast<std::uint16_t>(chain::protocol::calculate_block_num_from_id(value));
}

std::uint32_t reference_block_prefix(const chain::protocol::block_id& value) noexcept {
   return static_cast<std::uint32_t>(value._hash[1] & 0xffffffffULL);
}

builder::builder(context context, options options) : context_(std::move(context)), options_(options) {
   validate(context_, options_);
}

builder& builder::add_action(chain::protocol::action value) {
   actions_.push_back(std::move(value));
   return *this;
}

builder& builder::add_context_free_action(chain::protocol::action value) {
   context_free_actions_.push_back(std::move(value));
   return *this;
}

builder& builder::add_context_free_data(chain::protocol::bytes value) {
   context_free_data_.push_back(std::move(value));
   return *this;
}

builder& builder::add_extension(chain::protocol::transaction_extension value) {
   extensions_.push_back(std::move(value));
   return *this;
}

const context& builder::chain_context() const noexcept {
   return context_;
}

const options& builder::build_options() const noexcept {
   return options_;
}

unsigned_transaction builder::build() const {
   if (actions_.empty() && context_free_actions_.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::missing_action, "transaction must contain at least one action");
   }

   auto value = chain::protocol::transaction{};
   value.expiration = options_.expiration.value_or(chain::protocol::time_point_sec{
       context_.reference_time.sec_since_epoch() + options_.expiration_seconds});
   value.ref_block_num = reference_block_num(context_.reference_block);
   value.ref_block_prefix = reference_block_prefix(context_.reference_block);
   value.max_net_usage_words = static_cast<std::uint32_t>(
       (static_cast<std::uint64_t>(options_.max_net_usage_bytes) + 7U) / 8U);
   value.max_cpu_usage_ms = options_.max_cpu_usage_ms;
   value.delay_sec = options_.delay_seconds;
   value.context_free_actions = context_free_actions_;
   value.actions = actions_;
   value.transaction_extensions = chain::protocol::extensions{};
   value.transaction_extensions.reserve(extensions_.size());
   for (const auto& extension : extensions_) {
      std::visit(
          [&](const auto& typed) {
             value.transaction_extensions.emplace_back(typed.extension_id(), forge::raw::pack(typed));
          },
          extension);
   }

   return unsigned_transaction{
       .chain = context_.chain,
       .value = std::move(value),
       .context_free_data = context_free_data_,
       .compression = options_.compression,
   };
}

} // namespace forge::chain::transaction

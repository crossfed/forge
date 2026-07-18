module;

#include <cstdint>
#include <variant>
#include <vector>

export module forge.chain.protocol.transaction:value;

export import forge.chain.protocol.action;
export import forge.raw.varint_value;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct deferred_transaction_generation_context {
   static constexpr std::uint16_t extension_id() {
      return 0;
   }

   transaction_id sender_trx_id;
   uint128_t sender_id = 0;
   account_name sender;
};

using transaction_extension = std::variant<deferred_transaction_generation_context>;

struct transaction_header {
   time_point_sec expiration;
   std::uint16_t ref_block_num = 0;
   std::uint32_t ref_block_prefix = 0;
   forge::unsigned_int max_net_usage_words = 0;
   std::uint8_t max_cpu_usage_ms = 0;
   forge::unsigned_int delay_sec = 0;
};

struct transaction : transaction_header {
   std::vector<action> context_free_actions;
   std::vector<action> actions;
   extensions transaction_extensions;

#if !defined(FORGE_CONTRACT_GUEST)
   [[nodiscard]] transaction_id id() const;
   [[nodiscard]] digest sig_digest(const chain_id& chain_id, const std::vector<bytes>& cfd = {}) const;
#endif
};

template <typename Stream> void raw_pack(Stream& stream, const deferred_transaction_generation_context& value) {
   forge::raw::pack(stream, value.sender_trx_id);
   forge::raw::pack(stream, value.sender_id);
   forge::raw::pack(stream, value.sender);
}

template <typename Stream> void raw_unpack(Stream& stream, deferred_transaction_generation_context& value) {
   forge::raw::unpack(stream, value.sender_trx_id);
   forge::raw::unpack(stream, value.sender_id);
   forge::raw::unpack(stream, value.sender);
}

template <typename Stream> void raw_pack(Stream& stream, const transaction_header& value) {
   forge::raw::pack(stream, value.expiration);
   forge::raw::pack(stream, value.ref_block_num);
   forge::raw::pack(stream, value.ref_block_prefix);
   forge::raw::pack(stream, value.max_net_usage_words);
   forge::raw::pack(stream, value.max_cpu_usage_ms);
   forge::raw::pack(stream, value.delay_sec);
}

template <typename Stream> void raw_unpack(Stream& stream, transaction_header& value) {
   forge::raw::unpack(stream, value.expiration);
   forge::raw::unpack(stream, value.ref_block_num);
   forge::raw::unpack(stream, value.ref_block_prefix);
   forge::raw::unpack(stream, value.max_net_usage_words);
   forge::raw::unpack(stream, value.max_cpu_usage_ms);
   forge::raw::unpack(stream, value.delay_sec);
}

template <typename Stream> void raw_pack(Stream& stream, const transaction& value) {
   raw_pack(stream, static_cast<const transaction_header&>(value));
   forge::raw::pack(stream, value.context_free_actions);
   forge::raw::pack(stream, value.actions);
   forge::raw::pack(stream, value.transaction_extensions);
}

template <typename Stream> void raw_unpack(Stream& stream, transaction& value) {
   raw_unpack(stream, static_cast<transaction_header&>(value));
   forge::raw::unpack(stream, value.context_free_actions);
   forge::raw::unpack(stream, value.actions);
   forge::raw::unpack(stream, value.transaction_extensions);
}

} // namespace forge::chain::protocol

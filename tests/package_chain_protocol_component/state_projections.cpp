#include <concepts>
#include <optional>

import forge.chain.protocol.code;
import forge.chain.protocol.currency_stats;
import forge.chain.protocol.finalizer_vote_record;
import forge.chain.protocol.generated_transaction;
import forge.chain.protocol.producer_info;
import forge.chain.protocol.table;

bool state_projection_package_contract() {
   using namespace forge::chain::protocol;

   static_assert(std::same_as<code_id, forge::db::ids::typed_id<1, 13>>);
   static_assert(std::same_as<table_id, forge::db::ids::typed_id<1, 30>>);
   static_assert(std::same_as<generated_transaction_id, forge::db::ids::typed_id<1, 51>>);
   static_assert(std::same_as<decltype(generated_transaction{}.transaction), packed_transaction>);
   static_assert(std::same_as<decltype(producer_info{}.total_votes), float64>);
   static_assert(std::same_as<decltype(producer_info{}.producer_authority), std::optional<block_signing_authority>>);
   static_assert(std::same_as<decltype(finalizer_vote_record{}.public_key), forge::crypto::bls::public_key>);

   const auto code_value = code{.id = code_id{1U}, .code_size = 2U};
   const auto table_value = table{.id = table_id{3U}, .count = 4U};
   const auto stats = currency_stats{};
   auto packed = packed_transaction{};
   packed.packed_trx = {0x01U};
   const auto generated = generated_transaction{
       .id = generated_transaction_id{5U},
       .transaction = packed,
   };
   const auto producer = producer_info{};
   const auto vote = finalizer_vote_record{};

   return code_value.id.instance == 1U && code_value.code_size == 2U && table_value.id.instance == 3U &&
          table_value.count == 4U && stats == currency_stats{} && generated.transaction.packed_trx.size() == 1U &&
          producer.is_active && !producer.producer_authority && vote == finalizer_vote_record{};
}

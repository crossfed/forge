#include <forge/contract/intrinsics.h>

#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

import forge.contract;
import forge.contract.action;
import forge.contract.authorization;
import forge.contract.call;
import forge.contract.crypto;
import forge.contract.crypto_ext;
import forge.contract.deferred_transaction;
import forge.contract.instant_finality;
import forge.contract.print;
import forge.contract.privileged;
import forge.contract.producer_schedule;
import forge.contract.system;
import forge.contract.transaction;

namespace {

using forge::chain::protocol::make_name;

constexpr auto oracle_account = make_name("oracle");
constexpr auto alice_account = make_name("alice");
constexpr auto callee_account = make_name("callee");
constexpr auto active_permission = make_name("active");

} // namespace

class [[forge::contract("oracle")]] oracle : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] std::uint64_t run(std::uint32_t scenario) {
      switch (scenario) {
      case 0:
         return context_api();
      case 1:
         return print_api();
      case 2:
         return crypto_api();
      case 3:
         return crypto_extensions();
      case 4:
         return queue_api();
      case 5:
         return privileged_api();
      case 6:
         return transaction_api();
      case 7:
         return authorization_api();
      case 8:
         return finality_api();
      case 9:
         return synchronous_call();
      case 10:
         return rollback_api();
      case 11:
         return malformed_memory();
      default:
         forge::contract::check(false, "unknown oracle scenario");
      }
      return 0U;
   }

   [[forge::call]] std::uint32_t sum(std::uint32_t left, std::uint32_t right) const {
      return left + right;
   }

 private:
   std::uint64_t context_api() const {
      forge::contract::check(get_self() == oracle_account, "unexpected receiver");
      forge::contract::check(forge::contract::current_receiver() == oracle_account, "unexpected current receiver");
      forge::contract::check(forge::contract::publication_time().time_since_epoch().count() == 111,
                             "unexpected publication time");
      forge::contract::check(forge::contract::current_time_point().time_since_epoch().count() == 222,
                             "unexpected current time");
      forge::contract::check(forge::contract::current_block_number() == 333U, "unexpected block number");
      forge::contract::check(forge::contract::get_sender() == alice_account, "unexpected sender");
      forge::contract::require_auth(oracle_account);
      forge::contract::check(forge::contract::has_auth(oracle_account), "receiver authorization is missing");
      forge::contract::check(forge::contract::is_account(alice_account), "configured account is missing");
      forge::contract::require_recipient(alice_account);
      const auto producers = forge::contract::get_active_producers();
      forge::contract::check(producers.size() == 2U && producers[0] == oracle_account && producers[1] == alice_account,
                             "unexpected active producers");
      return 1U;
   }

   std::uint64_t print_api() const {
      forge::contract::print("oracle:", std::int64_t{-7}, ':', std::uint64_t{42}, ':', oracle_account);
      const auto bytes = std::array<std::uint8_t, 3>{0xde, 0xad, 0xbe};
      forge::contract::printhex(bytes.data(), bytes.size());
      auto minimum = -(__int128{1} << 126);
      minimum *= 2;
      ::printi128(&minimum);
      return 2U;
   }

   std::uint64_t crypto_api() const {
      constexpr auto data = std::array<char, 3>{'a', 'b', 'c'};
      const auto hash256 = forge::contract::sha256(data.data(), data.size());
      const auto hash1 = forge::contract::sha1(data.data(), data.size());
      const auto hash512 = forge::contract::sha512(data.data(), data.size());
      const auto ripemd = forge::contract::ripemd160(data.data(), data.size());
      forge::contract::assert_sha256(data.data(), data.size(), hash256);
      forge::contract::assert_sha1(data.data(), data.size(), hash1);
      forge::contract::assert_sha512(data.data(), data.size(), hash512);
      forge::contract::assert_ripemd160(data.data(), data.size(), ripemd);
      forge::contract::check(hash256 != forge::contract::checksum256{}, "sha256 returned zero");
      return 3U;
   }

   std::uint64_t crypto_extensions() const {
      constexpr auto data = std::array<char, 3>{'a', 'b', 'c'};
      const auto sha3 = forge::contract::sha3(data.data(), data.size());
      const auto keccak = forge::contract::keccak(data.data(), data.size());
      forge::contract::assert_sha3(data.data(), data.size(), sha3);
      forge::contract::assert_keccak(data.data(), data.size(), keccak);
      forge::contract::check(sha3 != keccak, "SHA3 and Keccak must differ");

      const auto base = forge::contract::bigint{2};
      const auto exponent = forge::contract::bigint{5};
      const auto modulus = forge::contract::bigint{13};
      auto result = forge::contract::bigint(1U);
      forge::contract::check(forge::contract::mod_exp(base, exponent, modulus, result) == forge::contract::success,
                             "mod_exp failed");
      forge::contract::check(static_cast<unsigned char>(result[0]) == 6U, "mod_exp returned wrong value");
      return 4U;
   }

   std::uint64_t queue_api() const {
      const auto inline_action = forge::contract::action{std::vector<forge::contract::permission_level>{},
                                                         oracle_account, make_name("noop"), std::tuple{42U}};
      inline_action.send();
      inline_action.send_context_free();

      const auto sender_id = forge::chain::protocol::uint128_t{7U};
      constexpr auto packed = std::array<char, 3>{1, 2, 3};
      forge::contract::send_deferred(sender_id, oracle_account, packed.data(), packed.size());
      forge::contract::check(forge::contract::cancel_deferred(sender_id), "deferred transaction was not cancelled");
      return 5U;
   }

   std::uint64_t privileged_api() const {
      forge::contract::set_privileged(alice_account, true);
      forge::contract::set_resource_limits(alice_account, 1024, 20, 30);
      auto parameters = forge::contract::blockchain_parameters{};
      parameters.max_block_net_usage = 4096;
      parameters.max_transaction_cpu_usage = 77;
      forge::contract::set_blockchain_parameters(parameters);
      auto loaded = forge::contract::blockchain_parameters{};
      forge::contract::get_blockchain_parameters(loaded);
      forge::contract::check(loaded.max_block_net_usage == 4096 && loaded.max_transaction_cpu_usage == 77,
                             "blockchain parameter round-trip failed");
      const auto feature = forge::contract::sha256("feature", 7U);
      forge::contract::preactivate_feature(feature);
      forge::contract::check(forge::contract::is_feature_activated(feature), "feature was not activated");
      return 6U;
   }

   std::uint64_t transaction_api() const {
      forge::contract::check(forge::contract::transaction_size() == 4U, "unexpected transaction size");
      auto bytes = std::array<char, 4>{};
      forge::contract::check(forge::contract::read_transaction(bytes.data(), bytes.size()) == bytes.size(),
                             "failed to read transaction");
      forge::contract::check(bytes == std::array<char, 4>{1, 2, 3, 4}, "unexpected transaction bytes");
      forge::contract::check(forge::contract::tapos_block_num() == 12, "unexpected TAPOS block number");
      forge::contract::check(forge::contract::tapos_block_prefix() == 34, "unexpected TAPOS block prefix");
      forge::contract::check(forge::contract::expiration() == 56U, "unexpected expiration");
      auto context_free = std::array<char, 3>{};
      forge::contract::check(forge::contract::get_context_free_data(0U, context_free.data(), context_free.size()) == 3,
                             "failed to read context-free data");
      forge::contract::check(context_free == std::array<char, 3>{5, 6, 7}, "unexpected context-free data");
      return 7U;
   }

   std::uint64_t authorization_api() const {
      forge::contract::check(forge::contract::check_transaction_authorization(nullptr, 0U, nullptr, 0U, nullptr, 0U),
                             "transaction authorization failed");
      forge::contract::check(
          forge::contract::check_permission_authorization(alice_account, active_permission, nullptr, 0U, nullptr, 0U),
          "permission authorization failed");
      forge::contract::check(
          forge::contract::get_permission_last_used(alice_account, active_permission).time_since_epoch().count() == 444,
          "unexpected permission use time");
      forge::contract::check(forge::contract::get_account_creation_time(alice_account).time_since_epoch().count() ==
                                 555,
                             "unexpected account creation time");
      return 8U;
   }

   std::uint64_t finality_api() const {
      auto policy = forge::contract::finalizer_policy{};
      policy.threshold = 0;
      forge::contract::set_finalizers(policy);
      return 9U;
   }

   std::uint64_t synchronous_call() const {
      const auto call = forge::contract::call_wrapper<"sum"_i, &oracle::sum>{callee_account};
      forge::contract::check(call(20U, 22U) == 42U, "synchronous call returned wrong value");
      return 10U;
   }

   std::uint64_t rollback_api() const {
      forge::contract::print("rollback");
      forge::contract::require_recipient(alice_account);
      const auto inline_action = forge::contract::action{std::vector<forge::contract::permission_level>{},
                                                         oracle_account, make_name("noop"), std::tuple{1U}};
      inline_action.send();
      forge::contract::set_privileged(alice_account, true);
      forge::contract::check(false, "oracle rollback requested");
      return 0U;
   }

   std::uint64_t malformed_memory() const {
      static_cast<void>(::read_action_data(reinterpret_cast<void*>(0xfffffff0U), 32U));
      return 0U;
   }
};

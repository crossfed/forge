#pragma once

#include <concepts>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#if defined(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX)
#include <eosio/eosio.hpp>
namespace contract_api = eosio;
#else
#include <forge/contract/serialize.hpp>
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.values;
import forge.contract.intrinsics;
import forge.contract.multi_index;
import forge.contract.singleton;
namespace contract_api = forge::contract;
#endif

namespace multi_index_cases {

using forge::chain::protocol::key256;
using forge::chain::protocol::name;
using forge::chain::protocol::uint128_t;
using forge::chain::protocol::literals::operator""_n;

#if defined(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX)
struct [[eosio::table("records")]] record {
#else
struct [[forge::table("records")]] record {
#endif
   std::uint64_t id = 0;
   std::uint64_t secondary64 = 0;
   uint128_t secondary128 = 0;
   key256 secondary256{};
   double secondary_double = 0;
   long double secondary_long_double = 0;
   std::string value;

   [[nodiscard]] std::uint64_t primary_key() const {
      return id;
   }

   [[nodiscard]] std::uint64_t by_64() const {
      return secondary64;
   }

   [[nodiscard]] uint128_t by_128() const {
      return secondary128;
   }

   [[nodiscard]] const key256& by_256() const {
      return secondary256;
   }

   [[nodiscard]] double by_double() const {
      return secondary_double;
   }

   [[nodiscard]] long double by_long_double() const {
      return secondary_long_double;
   }

#if defined(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX)
   EOSLIB_SERIALIZE(record,
                    (id)(secondary64)(secondary128)(secondary256)(secondary_double)(secondary_long_double)(value))
#else
   FORGE_SERIALIZE(record, &record::id, &record::secondary64, &record::secondary128, &record::secondary256,
                   &record::secondary_double, &record::secondary_long_double, &record::value)
#endif
};

using by_one = contract_api::indexed_by<"byone"_n, contract_api::const_mem_fun<record, std::uint64_t, &record::by_64>>;
using by_two = contract_api::indexed_by<"bytwo"_n, contract_api::const_mem_fun<record, uint128_t, &record::by_128>>;
using by_three =
    contract_api::indexed_by<"bythree"_n, contract_api::const_mem_fun<record, const key256&, &record::by_256>>;
using by_double =
    contract_api::indexed_by<"bydouble"_n, contract_api::const_mem_fun<record, double, &record::by_double>>;
using by_long_double =
    contract_api::indexed_by<"bylongdbl"_n, contract_api::const_mem_fun<record, long double, &record::by_long_double>>;

using records = contract_api::multi_index<"records"_n, record, by_one, by_two, by_three, by_double, by_long_double>;
using other_records = contract_api::multi_index<"other"_n, record, by_one>;
using exhausted_records = contract_api::multi_index<"exhaust"_n, record>;
using autoincrement_records = contract_api::multi_index<"autoinc"_n, record, by_two>;
using floating_records = contract_api::multi_index<"floats"_n, record, by_double, by_long_double>;
using settings = contract_api::singleton<"settings"_n, std::string>;

struct named_record {
   name id{};
   std::string value;

   [[nodiscard]] name primary_key() const {
      return id;
   }

#if defined(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX)
   EOSLIB_SERIALIZE(named_record, (id)(value))
#else
   FORGE_SERIALIZE(named_record, &named_record::id, &named_record::value)
#endif
};

using named_records = contract_api::multi_index<"named"_n, named_record>;

using primary_iterator = records::const_iterator;
using secondary_iterator = decltype(std::declval<records&>().template get_index<"byone"_n>())::const_iterator;
static_assert(std::bidirectional_iterator<primary_iterator>);
static_assert(std::bidirectional_iterator<secondary_iterator>);
#if defined(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX)
static_assert(std::same_as<eosio::checksum256, key256>);
#endif

[[nodiscard]] constexpr key256 make_key(std::uint64_t value) {
   return key256::make_from_word_sequence(std::uint64_t{0}, std::uint64_t{0}, std::uint64_t{0}, value);
}

inline void set_record(record& row, std::uint64_t id, std::uint64_t secondary, const char* value) {
   row.id = id;
   row.secondary64 = secondary;
   row.secondary128 = static_cast<uint128_t>(secondary) * 10U;
   row.secondary256 = make_key(secondary);
   row.secondary_double = static_cast<double>(secondary) / 10.0;
   row.secondary_long_double = static_cast<long double>(secondary);
   row.value = value;
}

struct record_update {
   std::uint64_t id = 0;
   std::uint64_t secondary = 0;
   const char* value = nullptr;
   bool floating_only = false;

   void operator()(record& row) const {
      if (floating_only) {
         row.secondary_double = 0.0;
         row.secondary_long_double = 0.0L;
         return;
      }
      set_record(row, id, secondary, value);
   }
};

inline void run(name self, std::uint32_t scenario) {
   auto table = records{self, self.value};
   switch (scenario) {
#if FORGE_CONTRACT_MULTI_INDEX_SUITE == 0
   case 0: {
      table.emplace(self, [](record& row) { set_record(row, 1, 20, "first"); });
      table.emplace(self, [](record& row) { set_record(row, 2, 10, "second"); });
      table.emplace(self, [](record& row) {
         set_record(row, 3, 20, "third");
         row.secondary_double = -0.0;
         row.secondary_long_double = -0.0L;
      });

      contract_api::check(table.begin()->id == 1, "primary order mismatch");
      auto by64 = table.template get_index<"byone"_n>();
      auto iterator = by64.begin();
      contract_api::check(iterator->id == 2, "secondary first record mismatch");
      contract_api::check((++iterator)->id == 1, "secondary duplicate order mismatch");
      contract_api::check((++iterator)->id == 3, "secondary duplicate primary order mismatch");
      contract_api::check(++iterator == by64.end(), "secondary end mismatch");

      contract_api::check(table.template get_index<"bytwo"_n>().lower_bound(uint128_t{150})->id == 1,
                          "idx128 lower bound mismatch");
      auto by256 = table.template get_index<"bythree"_n>();
      contract_api::check(by256.begin()->id == 2, "idx256 first record mismatch");
      contract_api::check((++by256.begin())->id == 1, "idx256 duplicate order mismatch");
      contract_api::check(by256.upper_bound(make_key(10))->id == 1, "idx256 upper bound mismatch");
      contract_api::check(table.template get_index<"bydouble"_n>().lower_bound(1.5)->id == 1,
                          "idx_double lower bound mismatch");
      contract_api::check(table.template get_index<"bylongdbl"_n>().upper_bound(10.0L)->id == 1,
                          "idx_long_double upper bound mismatch");
      auto by_double = table.template get_index<"bydouble"_n>();
      const auto double_zero = by_double.find(0.0);
      contract_api::check(double_zero != by_double.end() && double_zero->id == 3,
                          "idx_double signed zero lookup mismatch");
      auto by_long_double = table.template get_index<"bylongdbl"_n>();
      const auto long_double_zero = by_long_double.find(0.0L);
      contract_api::check(long_double_zero != by_long_double.end() && long_double_zero->id == 3,
                          "idx_long_double signed zero lookup mismatch");
      return;
   }
   case 1: {
      const auto iterator = table.require_find(1);
      table.modify(iterator, contract_api::same_payer, record_update{1, 5, "updated", false});
      table.modify(table.require_find(3), contract_api::same_payer, record_update{0, 0, nullptr, true});
      contract_api::check(table.get(1).value == "updated", "modify failed");
      table.erase(table.require_find(2));
      contract_api::check(table.find(2) == table.end(), "erase failed");
      contract_api::check(table.template get_index<"byone"_n>().begin()->id == 1, "secondary reindex failed");
      return;
   }
#endif
#if FORGE_CONTRACT_MULTI_INDEX_SUITE == 2
   case 2: {
      auto value = settings{self, self.value};
      contract_api::check(!value.exists(), "new singleton exists");
      contract_api::check(value.get_or_default("default") == "default", "singleton default failed");
      contract_api::check(value.get_or_create(self, "created") == "created", "singleton create failed");
      value.set("configured", contract_api::same_payer);
      contract_api::check(value.exists() && value.get() == "configured", "singleton set failed");
      return;
   }
#endif
#if FORGE_CONTRACT_MULTI_INDEX_SUITE == 0
   case 3:
      contract_api::check(table.available_primary_key() == 4, "available primary key recovery failed");
      table.emplace(self, [&](record& row) { set_record(row, table.available_primary_key(), 40, "fourth"); });
      contract_api::check(table.available_primary_key() == 5, "available primary key advance failed");
      return;
   case 4: {
      auto reverse = table.rbegin();
      contract_api::check(reverse->id == 4, "primary reverse first mismatch");
      contract_api::check((++reverse)->id == 3, "primary reverse second mismatch");
      auto by64 = table.template get_index<"byone"_n>();
      contract_api::check(by64.find(20)->id == 3, "secondary find failed");
      const auto& loaded = table.get(3);
      contract_api::check(by64.iterator_to(loaded)->id == 3, "secondary iterator_to failed");
      contract_api::check(table.iterator_to(*by64.iterator_to(loaded))->id == 3, "primary cache lookup failed");
      contract_api::check((--table.iterator_to(loaded))->id == 1, "secondary-to-primary cache lookup failed");
      contract_api::check((++by64.iterator_to(table.get(1)))->id == 3, "primary-to-secondary cache lookup failed");
      auto secondary_reverse = by64.rbegin();
      contract_api::check(secondary_reverse->id == 4, "secondary reverse first mismatch");
      contract_api::check((++secondary_reverse)->id == 3, "secondary reverse second mismatch");
      contract_api::check(table.lower_bound(2)->id == 3, "primary lower bound mismatch");
      contract_api::check(table.upper_bound(3)->id == 4, "primary upper bound mismatch");
      return;
   }
   case 5: {
      auto end = table.end();
      ++end;
      return;
   }
   case 6:
      table.modify(table.require_find(1), self, [](record& row) { row.id = 100; });
      return;
   case 7: {
      auto other = other_records{self, self.value};
      other.iterator_to(*table.require_find(1));
      return;
   }
   case 8:
      table.emplace(self, [](record& row) { set_record(row, 10, 100, "rollback"); });
      contract_api::check(false, "rollback marker");
      return;
   case 9:
      table.modify(table.require_find(1), contract_api::same_payer, [](record& row) { row.value = "same payer"; });
      return;
   case 10: {
      auto exhausted = exhausted_records{self, self.value};
      exhausted.emplace(self, [](record& row) { row.id = std::numeric_limits<std::uint64_t>::max() - 3U; });
      contract_api::check(exhausted.available_primary_key() == std::numeric_limits<std::uint64_t>::max() - 2U,
                          "autoincrement near limit mismatch");
      exhausted.emplace(self, [&](record& row) { row.id = exhausted.available_primary_key(); });
      static_cast<void>(exhausted.available_primary_key());
      return;
   }
   case 33:
      table.modify(table.require_find(1), "alice"_n, [](record& row) { row.value = "new primary payer"; });
      return;
#elif FORGE_CONTRACT_MULTI_INDEX_SUITE == 1
   case 11:
      static_cast<void>(table.require_find(999));
      return;
   case 12:
      static_cast<void>(table.require_find(999, "unable to find primary key in require_find"));
      return;
   case 13:
      static_cast<void>(table.template get_index<"byone"_n>().require_find(999));
      return;
   case 14:
      static_cast<void>(table.template get_index<"byone"_n>().require_find(999, "unable to find sec key"));
      return;
   case 15: {
      auto begin = table.begin();
      --begin;
      return;
   }
   case 16: {
      auto end = table.template get_index<"byone"_n>().end();
      ++end;
      return;
   }
   case 17: {
      auto begin = table.template get_index<"byone"_n>().begin();
      --begin;
      return;
   }
   case 18:
      table.modify(table.end(), self, [](record&) {});
      return;
   case 19:
      table.erase(table.end());
      return;
   case 20: {
      auto index = table.template get_index<"byone"_n>();
      index.modify(index.end(), self, [](record&) {});
      return;
   }
   case 21: {
      auto index = table.template get_index<"byone"_n>();
      index.erase(index.end());
      return;
   }
   case 22: {
      auto other = other_records{self, self.value};
      other.template get_index<"byone"_n>().iterator_to(*table.require_find(1));
      return;
   }
   case 23: {
      auto other = other_records{self, self.value};
      other.modify(*table.require_find(1), self, [](record&) {});
      return;
   }
   case 24: {
      auto other = other_records{self, self.value};
      other.erase(*table.require_find(1));
      return;
   }
#elif FORGE_CONTRACT_MULTI_INDEX_SUITE == 2
   case 25: {
      auto floating = floating_records{self, self.value};
      for (auto id = std::uint64_t{1}; id <= 10U; ++id) {
         floating.emplace(self, [id](record& row) {
            row.id = id;
            row.secondary_double = 1.0 / (static_cast<double>(id) * 1'000'000.0);
            row.secondary_long_double = 1.0L / (static_cast<long double>(id) * 1'000'000.0L);
         });
      }

      auto expected = std::uint64_t{10};
      for (const auto& row : floating.template get_index<"bydouble"_n>()) {
         contract_api::check(row.id == expected--, "idx_double order mismatch");
      }
      contract_api::check(expected == 0U, "idx_double traversal mismatch");

      expected = 10U;
      for (const auto& row : floating.template get_index<"bylongdbl"_n>()) {
         contract_api::check(row.id == expected--, "idx_long_double order mismatch");
      }
      contract_api::check(expected == 0U, "idx_long_double traversal mismatch");

      const auto product = 1.0 / 1'000'000.0;
      contract_api::check(floating.template get_index<"bydouble"_n>().lower_bound(product / 5.5)->id == 5,
                          "idx_double lower bound mismatch");
      contract_api::check(floating.template get_index<"bydouble"_n>().upper_bound(product / 5.0)->id == 4,
                          "idx_double upper bound mismatch");
      const auto long_product = 1.0L / 1'000'000.0L;
      contract_api::check(floating.template get_index<"bylongdbl"_n>().lower_bound(long_product / 5.5L)->id == 5,
                          "idx_long_double lower bound mismatch");
      contract_api::check(floating.template get_index<"bylongdbl"_n>().upper_bound(long_product / 5.0L)->id == 4,
                          "idx_long_double upper bound mismatch");
      return;
   }
   case 26: {
      auto automatic = autoincrement_records{self, self.value};
      for (auto count = 0U; count < 3U; ++count) {
         automatic.emplace(self, [&](record& row) {
            row.id = automatic.available_primary_key();
            row.secondary128 = 1000U - row.id;
         });
      }
      automatic.erase(automatic.get(0));
      contract_api::check(automatic.template get_index<"bytwo"_n>().begin()->id == 2,
                          "autoincrement secondary order mismatch");
      return;
   }
   case 27: {
      auto automatic = autoincrement_records{self, self.value};
      contract_api::check(automatic.available_primary_key() == 3U, "autoincrement recovery mismatch");
      automatic.emplace(self, [](record& row) {
         row.id = 0;
         row.secondary128 = 1000U;
      });
      contract_api::check(automatic.available_primary_key() == 3U, "manual key changed autoincrement state");
      for (auto count = 0U; count < 2U; ++count) {
         automatic.emplace(self, [&](record& row) {
            row.id = automatic.available_primary_key();
            row.secondary128 = 1000U - row.id;
         });
      }
      const auto third = automatic.require_find(3);
      automatic.emplace(self, [&](record& row) {
         row.id = 100;
         row.secondary128 = third->secondary128;
      });
      automatic.erase(third);
      contract_api::check(automatic.available_primary_key() == 101U, "autoincrement custom-key recovery mismatch");
      return;
   }
#endif
#if FORGE_CONTRACT_MULTI_INDEX_SUITE == 2
   case 28: {
      auto value = settings{self, self.value};
      contract_api::check(value.exists() && value.get() == "configured", "singleton persisted value mismatch");
      value.remove();
      contract_api::check(!value.exists(), "singleton remove failed");
      return;
   }
   case 29:
      static_cast<void>(settings{self, self.value}.get());
      return;
   case 32: {
      auto named = named_records{self, self.value};
      named.emplace(self, [](named_record& row) {
         row.id = "bob"_n;
         row.value = "second";
      });
      named.emplace(self, [](named_record& row) {
         row.id = "alice"_n;
         row.value = "first";
      });
      contract_api::check(named.begin()->id == "alice"_n, "name primary order mismatch");
      contract_api::check(named.find("bob"_n)->value == "second", "name primary find mismatch");
      contract_api::check(named.lower_bound("bob"_n)->id == "bob"_n, "name primary lower bound mismatch");
      contract_api::check(named.upper_bound("alice"_n)->id == "bob"_n, "name primary upper bound mismatch");
      return;
   }
#endif
#if FORGE_CONTRACT_MULTI_INDEX_SUITE == 1
   case 30:
      table.iterator_to(*table.end());
      return;
   case 31: {
      auto index = table.template get_index<"byone"_n>();
      index.iterator_to(*index.end());
      return;
   }
#endif
   default:
      contract_api::check(false, "unknown multi_index scenario");
   }
}

} // namespace multi_index_cases

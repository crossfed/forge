#include <forge/contract/intrinsics.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

import forge.contract;

namespace {

constexpr auto primary_scope = std::uint64_t{1};
constexpr auto primary_table = std::uint64_t{2};
constexpr auto index64_table = std::uint64_t{3};
constexpr auto index128_table = std::uint64_t{4};
constexpr auto index256_table = std::uint64_t{5};
constexpr auto index_double_table = std::uint64_t{6};
constexpr auto index_long_double_table = std::uint64_t{7};
constexpr auto donor_primary_table = forge::chain::protocol::make_name("table1").value;
constexpr auto donor_primary_bounds_table = forge::chain::protocol::make_name("mytable").value;
constexpr auto donor_idx64_table = forge::chain::protocol::make_name("myindextable").value;
constexpr auto donor_idx128_table = forge::chain::protocol::make_name("indextable4").value;
constexpr auto donor_idx256_table = forge::chain::protocol::make_name("indextable5").value;
constexpr auto donor_idx_double_table = forge::chain::protocol::make_name("floattable1").value;
constexpr auto donor_idx_long_double_table = forge::chain::protocol::make_name("floattable2").value;
constexpr auto alice = forge::chain::protocol::make_name("alice").value;
constexpr auto allyson = forge::chain::protocol::make_name("allyson").value;
constexpr auto billy = forge::chain::protocol::make_name("billy").value;
constexpr auto bob = forge::chain::protocol::make_name("bob").value;
constexpr auto charlie = forge::chain::protocol::make_name("charlie").value;
constexpr auto emily = forge::chain::protocol::make_name("emily").value;
constexpr auto frank = forge::chain::protocol::make_name("frank").value;
constexpr auto joe = forge::chain::protocol::make_name("joe").value;
constexpr auto kevin = forge::chain::protocol::make_name("kevin").value;

void expect(bool condition, const char* message) {
   forge::contract::check(condition, message);
}

} // namespace

class [[forge::contract("dbhost")]] dbhost : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run(std::uint32_t scenario) {
      switch (scenario) {
      case 0:
         primary();
         return;
      case 1:
         secondary();
         return;
      case 2:
         rollback();
         return;
      case 3:
         exitcommit();
         return;
      case 4:
         badpayer();
         return;
      case 5:
         duplicate();
         return;
      case 6:
         baditerator();
         return;
      case 7:
         foreign();
         return;
      case 8:
         idxbad();
         return;
      case 9:
         nandouble();
         return;
      case 10:
         nanlong();
         return;
      case 11:
         misaligned256();
         return;
      case 12:
         nanlookup(0);
         return;
      case 13:
         nanlookup(1);
         return;
      case 14:
         nanlookup(2);
         return;
      case 15:
         nanmodify();
         return;
      case 16:
         wrongkind();
         return;
      case 17:
         zeroorder();
         return;
      case 18:
         emptyremove();
         return;
      case 19:
         removediterator();
         return;
      default:
         forge::contract::check(false, "unknown database host scenario");
      }
   }

 private:
   void primary() {
      constexpr char first[] = "first";
      constexpr char second[] = "second";
      constexpr char updated[] = "updated";
      const auto payer = get_self().value;

      const auto first_iterator = db_store_i64(primary_scope, primary_table, payer, 10, first, sizeof(first));
      const auto second_iterator = db_store_i64(primary_scope, primary_table, payer, 30, second, sizeof(second));
      expect(first_iterator >= 0 && second_iterator >= 0, "primary store failed");

      db_update_i64(second_iterator, 0, updated, sizeof(updated));
      char buffer[sizeof(updated)]{};
      expect(db_get_i64(second_iterator, buffer, sizeof(buffer)) == sizeof(updated), "primary get size failed");
      expect(__builtin_memcmp(buffer, updated, sizeof(updated)) == 0, "primary get value failed");

      expect(db_find_i64(payer, primary_scope, primary_table, 10) == first_iterator, "primary find failed");
      expect(db_lowerbound_i64(payer, primary_scope, primary_table, 11) == second_iterator,
             "primary lower bound failed");
      expect(db_upperbound_i64(payer, primary_scope, primary_table, 10) == second_iterator,
             "primary upper bound failed");

      std::uint64_t found = 0;
      expect(db_next_i64(first_iterator, &found) == second_iterator && found == 30, "primary next failed");
      const auto end = db_end_i64(payer, primary_scope, primary_table);
      expect(end < -1, "primary end failed");
      expect(db_previous_i64(end, &found) == second_iterator && found == 30, "primary previous failed");

      db_remove_i64(first_iterator);
      donor_primary_general(payer);
      donor_primary_bounds(payer);
   }

   void secondary() {
      const auto payer = get_self().value;
      exercise_idx64(payer);
      donor_idx64(payer);
      exercise_idx128(payer);
      donor_idx128(payer);
      exercise_idx256(payer);
      donor_idx256(payer);
      exercise_idx_double(payer);
      donor_idx_double(payer);
      exercise_idx_long_double(payer);
      donor_idx_long_double(payer);
   }

   void rollback() {
      constexpr char value[] = "rollback";
      db_store_i64(primary_scope, 8, get_self().value, 99, value, sizeof(value));
      forge::contract::check(false, "rollback requested");
   }

   void exitcommit() {
      constexpr char value[] = "exit";
      db_store_i64(primary_scope, 9, get_self().value, 77, value, sizeof(value));
      forge::contract::exit(0);
   }

   void badpayer() {
      constexpr char value[] = "bad";
      db_store_i64(primary_scope, 10, 0, 1, value, sizeof(value));
   }

   void duplicate() {
      constexpr char value[] = "duplicate";
      db_store_i64(primary_scope, 11, get_self().value, 1, value, sizeof(value));
      db_store_i64(primary_scope, 11, get_self().value, 1, value, sizeof(value));
   }

   void baditerator() {
      char value = 0;
      db_get_i64(0, &value, 1);
   }

   void foreign() {
      constexpr auto owner = forge::chain::protocol::make_name("dbhost").value;
      const auto iterator = db_find_i64(owner, primary_scope, primary_table, 30);
      constexpr char value[] = "foreign";
      db_update_i64(iterator, 0, value, sizeof(value));
   }

   void idxbad() {
      unsigned __int128 value[1]{20};
      db_idx256_store(primary_scope, 12, get_self().value, 1, value, 1);
   }

   void nandouble() {
      const auto value = std::numeric_limits<double>::quiet_NaN();
      db_idx_double_store(primary_scope, 13, get_self().value, 1, &value);
   }

   void nanlong() {
      const auto value = std::numeric_limits<long double>::quiet_NaN();
      db_idx_long_double_store(primary_scope, 14, get_self().value, 1, &value);
   }

   void misaligned256() {
      alignas(unsigned __int128) char storage[sizeof(unsigned __int128) * 2 + 1]{};
      const unsigned __int128 words[2]{42, 0};
      __builtin_memcpy(storage + 1, words, sizeof(words));
      auto* key = reinterpret_cast<unsigned __int128*>(storage + 1);
      const auto iterator = db_idx256_store(primary_scope, 15, get_self().value, 1, key, 2);
      unsigned __int128 output[2]{};
      expect(db_idx256_find_primary(get_self().value, primary_scope, 15, output, 2, 1) == iterator,
             "misaligned idx256 lookup failed");
      expect(output[0] == 42 && output[1] == 0, "misaligned idx256 output failed");
   }

   void nanlookup(std::uint32_t operation) {
      auto valid = 0.0;
      db_idx_double_store(primary_scope, 16, get_self().value, 1, &valid);
      auto value = std::numeric_limits<double>::quiet_NaN();
      std::uint64_t primary = 0;
      switch (operation) {
      case 0:
         db_idx_double_find_secondary(get_self().value, primary_scope, 16, &value, &primary);
         return;
      case 1:
         db_idx_double_lowerbound(get_self().value, primary_scope, 16, &value, &primary);
         return;
      default:
         db_idx_double_upperbound(get_self().value, primary_scope, 16, &value, &primary);
         return;
      }
   }

   void nanmodify() {
      auto value = 0.0;
      const auto iterator = db_idx_double_store(primary_scope, 17, get_self().value, 1, &value);
      value = std::numeric_limits<double>::quiet_NaN();
      db_idx_double_update(iterator, 0, &value);
   }

   void wrongkind() {
      constexpr char value[] = "wrong-kind";
      const auto iterator = db_store_i64(primary_scope, 18, get_self().value, 1, value, sizeof(value));
      db_idx64_remove(iterator);
   }

   void zeroorder() {
      auto negative = -0.0;
      auto positive = 0.0;
      const auto first = db_idx_double_store(primary_scope, 19, get_self().value, 10, &negative);
      const auto second = db_idx_double_store(primary_scope, 19, get_self().value, 20, &positive);
      std::uint64_t primary = 0;
      expect(db_idx_double_find_secondary(get_self().value, primary_scope, 19, &positive, &primary) == first &&
                 primary == 10,
             "signed zero lookup failed");
      expect(db_idx_double_next(first, &primary) == second && primary == 20, "signed zero ordering failed");
   }

   void emptyremove() {
      constexpr char value[] = "temporary";
      const auto iterator = db_store_i64(primary_scope, 20, get_self().value, 1, value, sizeof(value));
      db_remove_i64(iterator);
   }

   void removediterator() {
      constexpr char value[] = "removed";
      const auto iterator = db_store_i64(primary_scope, 21, get_self().value, 1, value, sizeof(value));
      db_remove_i64(iterator);
      char output = 0;
      db_get_i64(iterator, &output, 1);
   }

   static void donor_primary_general(std::uint64_t payer) {
      constexpr char alice_info[] = "alice's info";
      constexpr char bob_info[] = "bob's info";
      constexpr char charlie_info[] = "charlie's info";
      constexpr char allyson_info[] = "allyson's info";
      constexpr char bob_updated[] = "bob's new info";

      const auto alice_iterator =
          db_store_i64(payer, donor_primary_table, payer, alice, alice_info, sizeof(alice_info) - 1U);
      db_store_i64(payer, donor_primary_table, payer, bob, bob_info, sizeof(bob_info) - 1U);
      db_store_i64(payer, donor_primary_table, payer, charlie, charlie_info, sizeof(charlie_info) - 1U);
      db_store_i64(payer, donor_primary_table, payer, allyson, allyson_info, sizeof(allyson_info) - 1U);

      auto primary = std::uint64_t{};
      auto iterator = db_next_i64(alice_iterator, &primary);
      expect(iterator == db_find_i64(payer, payer, donor_primary_table, allyson) && primary == allyson,
             "primary donor first next failed");
      iterator = db_next_i64(iterator, &primary);
      expect(iterator == db_find_i64(payer, payer, donor_primary_table, bob) && primary == bob,
             "primary donor second next failed");

      auto charlie_iterator = db_find_i64(payer, payer, donor_primary_table, charlie);
      primary = 0;
      expect(db_next_i64(charlie_iterator, &primary) < 0 && primary == 0, "primary donor end traversal failed");

      iterator = db_previous_i64(charlie_iterator, &primary);
      expect(iterator == db_find_i64(payer, payer, donor_primary_table, bob) && primary == bob,
             "primary donor first previous failed");
      iterator = db_previous_i64(iterator, &primary);
      expect(iterator == db_find_i64(payer, payer, donor_primary_table, allyson) && primary == allyson,
             "primary donor second previous failed");
      iterator = db_previous_i64(iterator, &primary);
      expect(iterator == db_find_i64(payer, payer, donor_primary_table, alice) && primary == alice,
             "primary donor third previous failed");
      expect(db_previous_i64(iterator, &primary) < 0 && primary == alice, "primary donor begin traversal failed");

      iterator = db_find_i64(payer, payer, donor_primary_table, alice);
      expect(iterator >= 0, "primary donor remove lookup failed");
      db_remove_i64(iterator);
      expect(db_find_i64(payer, payer, donor_primary_table, alice) < 0, "primary donor remove failed");

      iterator = db_find_i64(payer, payer, donor_primary_table, bob);
      char buffer[32]{};
      expect(db_get_i64(iterator, buffer, 5) == 5 && __builtin_memcmp(buffer, "bob's", 5) == 0,
             "primary donor partial get failed");
      const auto size = db_get_i64(iterator, buffer, 0);
      expect(size == sizeof(bob_info) - 1U, "primary donor size query failed");
      expect(db_get_i64(iterator, buffer, size) == size && __builtin_memcmp(buffer, bob_info, size) == 0,
             "primary donor full get failed");

      db_update_i64(iterator, payer, bob_updated, sizeof(bob_updated) - 1U);
      expect(db_get_i64(iterator, buffer, sizeof(bob_updated) - 1U) == sizeof(bob_updated) - 1U &&
                 __builtin_memcmp(buffer, bob_updated, sizeof(bob_updated) - 1U) == 0,
             "primary donor update failed");
   }

   static void donor_primary_bounds(std::uint64_t payer) {
      constexpr char value[] = "value";
      for (const auto primary : {alice, bob, charlie, emily, allyson, joe}) {
         db_store_i64(payer, donor_primary_bounds_table, payer, primary, value, sizeof(value) - 1U);
      }

      expect(db_lowerbound_i64(payer, payer, donor_primary_bounds_table, alice) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, alice),
             "primary donor exact lower bound failed");
      expect(db_lowerbound_i64(payer, payer, donor_primary_bounds_table, billy) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, bob),
             "primary donor middle lower bound failed");
      expect(db_lowerbound_i64(payer, payer, donor_primary_bounds_table, frank) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, joe),
             "primary donor upper lower bound failed");
      expect(db_lowerbound_i64(payer, payer, donor_primary_bounds_table, joe) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, joe),
             "primary donor last lower bound failed");
      expect(db_lowerbound_i64(payer, payer, donor_primary_bounds_table, kevin) < 0,
             "primary donor missing lower bound failed");

      expect(db_upperbound_i64(payer, payer, donor_primary_bounds_table, alice) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, allyson),
             "primary donor exact upper bound failed");
      expect(db_upperbound_i64(payer, payer, donor_primary_bounds_table, billy) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, bob),
             "primary donor middle upper bound failed");
      expect(db_upperbound_i64(payer, payer, donor_primary_bounds_table, frank) ==
                 db_find_i64(payer, payer, donor_primary_bounds_table, joe),
             "primary donor upper upper bound failed");
      expect(db_upperbound_i64(payer, payer, donor_primary_bounds_table, joe) < 0,
             "primary donor last upper bound failed");
      expect(db_upperbound_i64(payer, payer, donor_primary_bounds_table, kevin) < 0,
             "primary donor missing upper bound failed");
   }

   static void exercise_idx64(std::uint64_t payer) {
      auto first = std::uint64_t{20};
      auto second = std::uint64_t{40};
      const auto first_iterator = db_idx64_store(primary_scope, index64_table, payer, 10, &first);
      const auto second_iterator = db_idx64_store(primary_scope, index64_table, payer, 30, &second);
      second = 50;
      db_idx64_update(second_iterator, 0, &second);
      verify_idx64(payer, first_iterator, second_iterator);
      db_idx64_remove(first_iterator);
   }

   static void verify_idx64(std::uint64_t payer, std::int32_t first_iterator, std::int32_t second_iterator) {
      auto secondary = std::uint64_t{};
      auto primary = std::uint64_t{};
      expect(db_idx64_find_primary(payer, primary_scope, index64_table, &secondary, 10) == first_iterator &&
                 secondary == 20,
             "idx64 find primary failed");
      secondary = 50;
      expect(db_idx64_find_secondary(payer, primary_scope, index64_table, &secondary, &primary) == second_iterator &&
                 primary == 30,
             "idx64 find secondary failed");
      secondary = 21;
      expect(db_idx64_lowerbound(payer, primary_scope, index64_table, &secondary, &primary) == second_iterator &&
                 secondary == 50 && primary == 30,
             "idx64 lower bound failed");
      secondary = 20;
      expect(db_idx64_upperbound(payer, primary_scope, index64_table, &secondary, &primary) == second_iterator,
             "idx64 upper bound failed");
      expect(db_idx64_next(first_iterator, &primary) == second_iterator && primary == 30, "idx64 next failed");
      const auto end = db_idx64_end(payer, primary_scope, index64_table);
      expect(db_idx64_previous(end, &primary) == second_iterator && primary == 30, "idx64 previous failed");
   }

   static void donor_idx64(std::uint64_t payer) {
      struct record {
         std::uint64_t primary;
         std::uint64_t secondary;
      };
      constexpr record records[]{
          {265, alice}, {781, bob}, {234, charlie}, {650, allyson}, {540, bob}, {976, emily}, {110, joe},
      };
      for (const auto& value : records) {
         db_idx64_store(payer, donor_idx64_table, payer, value.primary, &value.secondary);
      }

      auto secondary = std::uint64_t{};
      auto primary = std::uint64_t{};
      auto iterator = db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 999);
      expect(iterator < 0 && secondary == 0, "idx64 donor missing primary failed");
      iterator = db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 110);
      expect(iterator >= 0 && secondary == joe, "idx64 donor primary lookup failed");
      expect(db_idx64_next(iterator, &primary) < 0 && primary == 0, "idx64 donor primary end failed");

      iterator = db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 234);
      expect(iterator >= 0 && secondary == charlie, "idx64 donor forward start failed");
      iterator = db_idx64_next(iterator, &primary);
      expect(iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 976) && primary == 976 &&
                 secondary == emily,
             "idx64 donor first next failed");
      iterator = db_idx64_next(iterator, &primary);
      expect(iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 110) && primary == 110 &&
                 secondary == joe,
             "idx64 donor second next failed");
      expect(db_idx64_next(iterator, &primary) < 0 && primary == 110, "idx64 donor final next failed");

      iterator = db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 781);
      expect(iterator >= 0 && secondary == bob, "idx64 donor backward start failed");
      iterator = db_idx64_previous(iterator, &primary);
      expect(iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 540) && primary == 540 &&
                 secondary == bob,
             "idx64 donor first previous failed");
      iterator = db_idx64_previous(iterator, &primary);
      expect(iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 650) && primary == 650 &&
                 secondary == allyson,
             "idx64 donor second previous failed");
      iterator = db_idx64_previous(iterator, &primary);
      expect(iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 265) && primary == 265 &&
                 secondary == alice,
             "idx64 donor third previous failed");
      expect(db_idx64_previous(iterator, &primary) < 0 && primary == 265, "idx64 donor final previous failed");

      secondary = bob;
      expect(db_idx64_find_secondary(payer, payer, donor_idx64_table, &secondary, &primary) >= 0 && primary == 540,
             "idx64 donor duplicate secondary lookup failed");
      secondary = emily;
      expect(db_idx64_find_secondary(payer, payer, donor_idx64_table, &secondary, &primary) >= 0 && primary == 976,
             "idx64 donor secondary lookup failed");
      secondary = frank;
      expect(db_idx64_find_secondary(payer, payer, donor_idx64_table, &secondary, &primary) < 0 && primary == 976,
             "idx64 donor missing secondary failed");

      verify_idx64_bounds(payer);
      verify_idx64_aliasing(payer);

      secondary = bob;
      iterator = db_idx64_store(payer, donor_idx64_table, payer, 421, &secondary);
      secondary = billy;
      db_idx64_update(iterator, payer, &secondary);
      auto found = std::uint64_t{};
      expect(db_idx64_find_primary(payer, payer, donor_idx64_table, &found, 421) == iterator && found == billy,
             "idx64 donor update failed");
      db_idx64_remove(iterator);
      expect(db_idx64_find_primary(payer, payer, donor_idx64_table, &found, 421) < 0, "idx64 donor remove failed");
   }

   static void verify_idx64_bounds(std::uint64_t payer) {
      auto secondary = alice;
      auto primary = std::uint64_t{};
      auto iterator = db_idx64_lowerbound(payer, payer, donor_idx64_table, &secondary, &primary);
      expect(primary == 265 && secondary == alice &&
                 iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 265),
             "idx64 donor exact lower bound failed");
      secondary = billy;
      iterator = db_idx64_lowerbound(payer, payer, donor_idx64_table, &secondary, &primary);
      expect(primary == 540 && secondary == bob &&
                 iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 540),
             "idx64 donor middle lower bound failed");
      secondary = joe;
      iterator = db_idx64_lowerbound(payer, payer, donor_idx64_table, &secondary, &primary);
      expect(primary == 110 && secondary == joe &&
                 iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 110),
             "idx64 donor last lower bound failed");
      secondary = kevin;
      primary = 0;
      expect(db_idx64_lowerbound(payer, payer, donor_idx64_table, &secondary, &primary) < 0 && primary == 0 &&
                 secondary == kevin,
             "idx64 donor missing lower bound failed");

      secondary = alice;
      iterator = db_idx64_upperbound(payer, payer, donor_idx64_table, &secondary, &primary);
      expect(primary == 650 && secondary == allyson &&
                 iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 650),
             "idx64 donor exact upper bound failed");
      secondary = billy;
      iterator = db_idx64_upperbound(payer, payer, donor_idx64_table, &secondary, &primary);
      expect(primary == 540 && secondary == bob &&
                 iterator == db_idx64_find_primary(payer, payer, donor_idx64_table, &secondary, 540),
             "idx64 donor middle upper bound failed");
      secondary = joe;
      primary = 0;
      expect(db_idx64_upperbound(payer, payer, donor_idx64_table, &secondary, &primary) < 0 && primary == 0 &&
                 secondary == joe,
             "idx64 donor last upper bound failed");
      secondary = kevin;
      expect(db_idx64_upperbound(payer, payer, donor_idx64_table, &secondary, &primary) < 0 && primary == 0 &&
                 secondary == kevin,
             "idx64 donor missing upper bound failed");
   }

   static void verify_idx64_alias(std::uint64_t payer, bool upper, std::size_t primary_offset,
                                  std::size_t secondary_offset, bool primary_wins) {
      alignas(std::uint64_t) char buffer[16]{};
      auto secondary = alice;
      __builtin_memcpy(buffer + secondary_offset, &secondary, sizeof(secondary));
      auto* secondary_output = reinterpret_cast<std::uint64_t*>(buffer + secondary_offset);
      auto* primary_output = reinterpret_cast<std::uint64_t*>(buffer + primary_offset);
      const auto iterator =
          upper ? db_idx64_upperbound(payer, payer, donor_idx64_table, secondary_output, primary_output)
                : db_idx64_lowerbound(payer, payer, donor_idx64_table, secondary_output, primary_output);
      expect(iterator >= 0, "idx64 donor overlapping bound failed");

      primary_output = reinterpret_cast<std::uint64_t*>(buffer + primary_offset);
      secondary_output = reinterpret_cast<std::uint64_t*>(buffer + secondary_offset);
      auto primary = std::uint64_t{};
      __builtin_memcpy(&primary, primary_output, sizeof(primary));
      __builtin_memcpy(&secondary, secondary_output, sizeof(secondary));
      const auto expected_primary = upper ? std::uint64_t{650} : std::uint64_t{265};
      const auto expected_secondary = upper ? allyson : alice;
      if (primary_wins) {
         expect(primary == expected_primary && secondary != expected_secondary,
                "idx64 donor overlapping primary write order failed");
      } else {
         expect(primary != expected_primary && secondary == expected_secondary,
                "idx64 donor overlapping secondary write order failed");
      }
   }

   static void verify_idx64_aliasing(std::uint64_t payer) {
      for (const auto upper : {false, true}) {
         verify_idx64_alias(payer, upper, 0, 0, false);
         verify_idx64_alias(payer, upper, 4, 4, true);
         verify_idx64_alias(payer, upper, 0, 1, false);
         verify_idx64_alias(payer, upper, 1, 0, true);
      }
   }

   static void exercise_idx128(std::uint64_t payer) {
      auto first = static_cast<unsigned __int128>(20);
      auto second = static_cast<unsigned __int128>(40);
      const auto first_iterator = db_idx128_store(primary_scope, index128_table, payer, 10, &first);
      const auto second_iterator = db_idx128_store(primary_scope, index128_table, payer, 30, &second);
      second = 50;
      db_idx128_update(second_iterator, 0, &second);
      unsigned __int128 secondary = 0;
      std::uint64_t primary = 0;
      expect(db_idx128_find_primary(payer, primary_scope, index128_table, &secondary, 10) == first_iterator &&
                 secondary == 20,
             "idx128 find primary failed");
      secondary = 50;
      expect(db_idx128_find_secondary(payer, primary_scope, index128_table, &secondary, &primary) == second_iterator &&
                 primary == 30,
             "idx128 find secondary failed");
      secondary = 21;
      expect(db_idx128_lowerbound(payer, primary_scope, index128_table, &secondary, &primary) == second_iterator &&
                 secondary == 50,
             "idx128 lower bound failed");
      secondary = 20;
      expect(db_idx128_upperbound(payer, primary_scope, index128_table, &secondary, &primary) == second_iterator,
             "idx128 upper bound failed");
      expect(db_idx128_next(first_iterator, &primary) == second_iterator && primary == 30, "idx128 next failed");
      const auto end = db_idx128_end(payer, primary_scope, index128_table);
      expect(db_idx128_previous(end, &primary) == second_iterator && primary == 30, "idx128 previous failed");
      db_idx128_remove(first_iterator);
   }

   static void donor_idx128(std::uint64_t payer) {
      const auto multiplier = static_cast<unsigned __int128>(1) << 63U;
      std::int32_t iterators[5]{};
      for (auto primary = std::uint64_t{}; primary < 5U; ++primary) {
         auto secondary = multiplier * primary;
         iterators[primary] = db_idx128_store(payer, donor_idx128_table, payer, primary, &secondary);
      }
      auto modified = multiplier * 6U;
      db_idx128_update(iterators[3], payer, &modified);

      auto secondary = static_cast<unsigned __int128>(0);
      auto primary = std::uint64_t{};
      auto iterator = db_idx128_lowerbound(payer, payer, donor_idx128_table, &secondary, &primary);
      constexpr std::uint64_t expected[]{0, 1, 2, 4, 3};
      for (const auto value : expected) {
         expect(iterator >= 0 && primary == value && secondary == multiplier * (value == 3 ? 6U : value),
                "idx128 donor secondary ordering failed");
         iterator = db_idx128_next(iterator, &primary);
         if (iterator >= 0) {
            db_idx128_find_primary(payer, payer, donor_idx128_table, &secondary, primary);
         }
      }
      expect(iterator < 0, "idx128 donor end traversal failed");
   }

   static void exercise_idx256(std::uint64_t payer) {
      unsigned __int128 first[2]{20, 0};
      unsigned __int128 second[2]{40, 0};
      const auto first_iterator = db_idx256_store(primary_scope, index256_table, payer, 10, first, 2);
      const auto second_iterator = db_idx256_store(primary_scope, index256_table, payer, 30, second, 2);
      second[0] = 50;
      db_idx256_update(second_iterator, 0, second, 2);
      unsigned __int128 secondary[2]{};
      std::uint64_t primary = 0;
      expect(db_idx256_find_primary(payer, primary_scope, index256_table, secondary, 2, 10) == first_iterator &&
                 secondary[0] == 20,
             "idx256 find primary failed");
      secondary[0] = 50;
      expect(db_idx256_find_secondary(payer, primary_scope, index256_table, secondary, 2, &primary) ==
                     second_iterator &&
                 primary == 30,
             "idx256 find secondary failed");
      secondary[0] = 21;
      expect(db_idx256_lowerbound(payer, primary_scope, index256_table, secondary, 2, &primary) == second_iterator &&
                 secondary[0] == 50,
             "idx256 lower bound failed");
      secondary[0] = 20;
      expect(db_idx256_upperbound(payer, primary_scope, index256_table, secondary, 2, &primary) == second_iterator,
             "idx256 upper bound failed");
      expect(db_idx256_next(first_iterator, &primary) == second_iterator && primary == 30, "idx256 next failed");
      const auto end = db_idx256_end(payer, primary_scope, index256_table);
      expect(db_idx256_previous(end, &primary) == second_iterator && primary == 30, "idx256 previous failed");
      db_idx256_remove(first_iterator);
   }

   static void donor_idx256(std::uint64_t payer) {
      unsigned __int128 forty_two[2]{42, 0};
      unsigned __int128 fifty[2]{50, 0};
      const auto first = db_idx256_store(payer, donor_idx256_table, payer, 1, forty_two, 2);
      const auto second = db_idx256_store(payer, donor_idx256_table, payer, 2, fifty, 2);
      db_idx256_store(payer, donor_idx256_table, payer, 3, forty_two, 2);

      unsigned __int128 secondary[2]{40, 0};
      auto primary = std::uint64_t{};
      expect(db_idx256_lowerbound(payer, payer, donor_idx256_table, secondary, 2, &primary) == first && primary == 1,
             "idx256 donor first lower bound failed");
      secondary[0] = 50;
      expect(db_idx256_lowerbound(payer, payer, donor_idx256_table, secondary, 2, &primary) == second && primary == 2,
             "idx256 donor second lower bound failed");

      secondary[0] = 42;
      auto iterator = db_idx256_find_secondary(payer, payer, donor_idx256_table, secondary, 2, &primary);
      expect(iterator == first && primary == 1, "idx256 donor duplicate lookup failed");
      iterator = db_idx256_next(iterator, &primary);
      expect(iterator >= 0 && primary == 3, "idx256 donor duplicate traversal failed");
      iterator = db_idx256_next(iterator, &primary);
      expect(iterator == second && primary == 2, "idx256 donor secondary ordering failed");
      expect(db_idx256_next(iterator, &primary) < 0, "idx256 donor end traversal failed");

      secondary[0] = 42;
      expect(db_idx256_upperbound(payer, payer, donor_idx256_table, secondary, 2, &primary) == second && primary == 2,
             "idx256 donor upper bound failed");
      db_idx256_remove(first);
      expect(db_idx256_find_primary(payer, payer, donor_idx256_table, secondary, 2, 1) < 0,
             "idx256 donor remove failed");
   }

   static void exercise_idx_double(std::uint64_t payer) {
      auto first = 20.0;
      auto second = 40.0;
      const auto first_iterator = db_idx_double_store(primary_scope, index_double_table, payer, 10, &first);
      const auto second_iterator = db_idx_double_store(primary_scope, index_double_table, payer, 30, &second);
      second = 50.0;
      db_idx_double_update(second_iterator, 0, &second);
      auto secondary = 0.0;
      std::uint64_t primary = 0;
      expect(db_idx_double_find_primary(payer, primary_scope, index_double_table, &secondary, 10) == first_iterator &&
                 secondary == 20.0,
             "idx_double find primary failed");
      secondary = 50.0;
      expect(db_idx_double_find_secondary(payer, primary_scope, index_double_table, &secondary, &primary) ==
                     second_iterator &&
                 primary == 30,
             "idx_double find secondary failed");
      secondary = 21.0;
      expect(db_idx_double_lowerbound(payer, primary_scope, index_double_table, &secondary, &primary) ==
                     second_iterator &&
                 secondary == 50.0,
             "idx_double lower bound failed");
      secondary = 20.0;
      expect(db_idx_double_upperbound(payer, primary_scope, index_double_table, &secondary, &primary) ==
                 second_iterator,
             "idx_double upper bound failed");
      expect(db_idx_double_next(first_iterator, &primary) == second_iterator && primary == 30,
             "idx_double next failed");
      const auto end = db_idx_double_end(payer, primary_scope, index_double_table);
      expect(db_idx_double_previous(end, &primary) == second_iterator && primary == 30, "idx_double previous failed");
      db_idx_double_remove(first_iterator);
   }

   static void donor_idx_double(std::uint64_t payer) {
      for (auto primary = std::uint64_t{1}; primary <= 10U; ++primary) {
         auto secondary = 1.0 / (static_cast<double>(primary) * 1'000'000.0);
         db_idx_double_store(payer, donor_idx_double_table, payer, primary, &secondary);
      }

      auto secondary = 0.0;
      auto primary = std::uint64_t{};
      auto iterator = db_idx_double_lowerbound(payer, payer, donor_idx_double_table, &secondary, &primary);
      for (auto expected = std::uint64_t{10}; expected > 0U; --expected) {
         expect(iterator >= 0 && primary == expected, "idx_double donor secondary ordering failed");
         iterator = db_idx_double_next(iterator, &primary);
      }
      expect(iterator < 0, "idx_double donor end traversal failed");

      constexpr auto product = 1.0 / 1'000'000.0;
      secondary = product / 5.5;
      expect(db_idx_double_lowerbound(payer, payer, donor_idx_double_table, &secondary, &primary) >= 0 && primary == 5,
             "idx_double donor lower bound failed");
      secondary = product / 5.0;
      expect(db_idx_double_upperbound(payer, payer, donor_idx_double_table, &secondary, &primary) >= 0 && primary == 4,
             "idx_double donor upper bound failed");
   }

   static void exercise_idx_long_double(std::uint64_t payer) {
      auto first = static_cast<long double>(20.0);
      auto second = static_cast<long double>(40.0);
      const auto first_iterator = db_idx_long_double_store(primary_scope, index_long_double_table, payer, 10, &first);
      const auto second_iterator = db_idx_long_double_store(primary_scope, index_long_double_table, payer, 30, &second);
      second = static_cast<long double>(50.0);
      db_idx_long_double_update(second_iterator, 0, &second);
      auto secondary = static_cast<long double>(0.0);
      std::uint64_t primary = 0;
      expect(db_idx_long_double_find_primary(payer, primary_scope, index_long_double_table, &secondary, 10) ==
                     first_iterator &&
                 secondary == static_cast<long double>(20.0),
             "idx_long_double find primary failed");
      secondary = static_cast<long double>(50.0);
      expect(db_idx_long_double_find_secondary(payer, primary_scope, index_long_double_table, &secondary, &primary) ==
                     second_iterator &&
                 primary == 30,
             "idx_long_double find secondary failed");
      secondary = static_cast<long double>(21.0);
      expect(db_idx_long_double_lowerbound(payer, primary_scope, index_long_double_table, &secondary, &primary) ==
                     second_iterator &&
                 secondary == static_cast<long double>(50.0),
             "idx_long_double lower bound failed");
      secondary = static_cast<long double>(20.0);
      expect(db_idx_long_double_upperbound(payer, primary_scope, index_long_double_table, &secondary, &primary) ==
                 second_iterator,
             "idx_long_double upper bound failed");
      expect(db_idx_long_double_next(first_iterator, &primary) == second_iterator && primary == 30,
             "idx_long_double next failed");
      const auto end = db_idx_long_double_end(payer, primary_scope, index_long_double_table);
      expect(db_idx_long_double_previous(end, &primary) == second_iterator && primary == 30,
             "idx_long_double previous failed");
      db_idx_long_double_remove(first_iterator);
   }

   static void donor_idx_long_double(std::uint64_t payer) {
      for (auto primary = std::uint64_t{1}; primary <= 10U; ++primary) {
         auto secondary = static_cast<long double>(1.0L) /
                          (static_cast<long double>(primary) * static_cast<long double>(1'000'000.0L));
         db_idx_long_double_store(payer, donor_idx_long_double_table, payer, primary, &secondary);
      }

      auto secondary = static_cast<long double>(0.0L);
      auto primary = std::uint64_t{};
      auto iterator = db_idx_long_double_lowerbound(payer, payer, donor_idx_long_double_table, &secondary, &primary);
      for (auto expected = std::uint64_t{10}; expected > 0U; --expected) {
         expect(iterator >= 0 && primary == expected, "idx_long_double donor secondary ordering failed");
         iterator = db_idx_long_double_next(iterator, &primary);
      }
      expect(iterator < 0, "idx_long_double donor end traversal failed");

      constexpr auto product = static_cast<long double>(1.0L) / static_cast<long double>(1'000'000.0L);
      secondary = product / static_cast<long double>(5.5L);
      expect(db_idx_long_double_lowerbound(payer, payer, donor_idx_long_double_table, &secondary, &primary) >= 0 &&
                 primary == 5,
             "idx_long_double donor lower bound failed");
      secondary = product / static_cast<long double>(5.0L);
      expect(db_idx_long_double_upperbound(payer, payer, donor_idx_long_double_table, &secondary, &primary) >= 0 &&
                 primary == 4,
             "idx_long_double donor upper bound failed");
   }
};

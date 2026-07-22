#include <eosio/internal/db.hpp>

#include <type_traits>

using idx128_store_signature = int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, const unsigned __int128*);
static_assert(std::is_same_v<decltype(&db_idx128_store), idx128_store_signature>);

void compile_eosio_internal_db_header() {
   unsigned __int128 secondary = 0;
   static_cast<void>(db_idx128_store(0, 0, 0, 0, &secondary));
}

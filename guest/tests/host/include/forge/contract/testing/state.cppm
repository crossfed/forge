module;

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module forge.contract.testing.state;

export import forge.crypto.sha256;

export namespace forge::contract::testing {

struct resource_limits {
   std::int64_t ram_bytes = -1;
   std::int64_t net_weight = -1;
   std::int64_t cpu_weight = -1;

   bool operator==(const resource_limits&) const = default;
};

struct code_hash {
   std::uint64_t sequence = 0;
   forge::crypto::sha256 digest;
   std::uint8_t vm_type = 0;
   std::uint8_t vm_version = 0;

   bool operator==(const code_hash&) const = default;
};

struct deferred_transaction {
   unsigned __int128 sender_id = 0;
   std::uint64_t payer = 0;
   std::vector<std::uint8_t> packed;

   bool operator==(const deferred_transaction&) const = default;
};

struct oracle_state {
   std::set<std::uint64_t> accounts;
   std::set<std::uint64_t> authorized_accounts;
   std::set<std::pair<std::uint64_t, std::uint64_t>> authorized_permissions;
   bool transaction_authorized = true;
   bool permission_authorized = true;

   std::vector<std::uint64_t> active_producers;
   std::uint64_t publication_time = 0;
   std::uint64_t current_time = 0;
   std::uint32_t block_num = 0;
   std::uint64_t sender = 0;

   std::map<std::uint64_t, code_hash> code_hashes;
   std::map<std::pair<std::uint64_t, std::uint64_t>, std::int64_t> permission_last_used;
   std::map<std::uint64_t, std::int64_t> account_creation_time;
   std::map<std::uint64_t, resource_limits> limits;
   std::set<std::uint64_t> privileged_accounts;
   std::set<forge::crypto::sha256> available_features;
   std::set<forge::crypto::sha256> activated_features;

   std::vector<std::uint8_t> transaction;
   std::int32_t tapos_block_num = 0;
   std::int32_t tapos_block_prefix = 0;
   std::uint32_t expiration = 0;
   std::array<std::vector<std::vector<std::uint8_t>>, 2> actions;
   std::vector<std::vector<std::uint8_t>> context_free_data;

   std::vector<std::uint64_t> recipients;
   std::vector<std::vector<std::uint8_t>> inline_actions;
   std::vector<std::vector<std::uint8_t>> context_free_inline_actions;
   std::map<unsigned __int128, deferred_transaction> deferred;
   std::vector<std::uint8_t> proposed_producers;
   std::uint64_t proposed_producer_version = 0;
   std::vector<std::uint8_t> blockchain_parameters;
   std::vector<std::uint8_t> kv_parameters;
   std::vector<std::uint8_t> wasm_parameters;
   std::array<std::vector<std::uint8_t>, 18> parameters;
   std::vector<std::uint8_t> finalizers;
   std::string console;

   bool operator==(const oracle_state&) const = default;
};

} // namespace forge::contract::testing

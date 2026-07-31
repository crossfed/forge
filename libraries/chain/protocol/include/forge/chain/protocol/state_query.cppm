module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.chain.protocol.state_query;

import forge.chain.protocol.action;
import forge.chain.protocol.code_hash_result;
import forge.chain.protocol.transaction;
import forge.variant.containers;
import forge.variant.described;

export import forge.chain.protocol.audit;
export import forge.variant.value;

export namespace forge::chain::protocol {

struct key_range {
   std::optional<bytes> lower;
   std::optional<bytes> upper;

   bool operator==(const key_range&) const = default;
};

struct state_point_request {
   bytes key;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const state_point_request&) const = default;
};

struct state_range_request {
   key_range range;
   std::optional<block_id> anchor;
   std::uint32_t limit = 256;
   audit_mode audit = audit_mode::none;

   bool operator==(const state_range_request&) const = default;
};

struct state_range_item {
   bytes key;
   bytes value;

   bool operator==(const state_range_item&) const = default;
};

struct state_point_response : audited_response {
   std::optional<bytes> value;

   bool operator==(const state_point_response&) const = default;
};

struct state_range_response : audited_response {
   std::vector<state_range_item> rows;
   std::optional<bytes> next_key;

   bool operator==(const state_range_response&) const = default;
};

struct state_mutation {
   bytes key;
   std::optional<bytes> value;

   bool operator==(const state_mutation&) const = default;
};

struct state_change_range {
   key_range range;
   std::vector<state_mutation> mutations;
   std::optional<bytes> next_key;

   bool operator==(const state_change_range&) const = default;
};

struct state_change_batch {
   state_anchor anchor;
   std::vector<state_change_range> ranges;

   bool operator==(const state_change_batch&) const = default;
};

struct state_changes_cursor {
   std::uint32_t block = 0;
   std::uint32_t range = 0;
   std::optional<bytes> key;

   bool operator==(const state_changes_cursor&) const = default;
};

struct state_changes_request {
   std::uint32_t from_block = 0;
   std::uint32_t to_block = 0;
   std::vector<key_range> ranges;
   std::uint32_t limit = 256;
   std::optional<state_changes_cursor> cursor;
   audit_mode audit = audit_mode::none;

   bool operator==(const state_changes_request&) const = default;
};

struct state_changes_response : audited_response {
   std::vector<state_change_batch> blocks;
   std::optional<state_changes_cursor> next;

   bool operator==(const state_changes_response&) const = default;
};

struct account_request {
   forge::chain::protocol::account_name account;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const account_request&) const = default;
};

struct account_response : audited_response {
   forge::chain::protocol::account_name account;
   forge::variant data;

   bool operator==(const account_response&) const = default;
};

struct code_request {
   forge::chain::protocol::account_name account;
   bool include_code = true;
   bool include_abi = true;
   std::optional<digest> known_abi_hash;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const code_request&) const = default;
};

struct code_response : audited_response {
   forge::chain::protocol::account_name account;
   forge::chain::protocol::code_hash_result hash;
   digest abi_hash;
   std::optional<bytes> wasm;
   std::optional<bytes> raw_abi;
   std::optional<forge::variant> abi;
};

struct table_rows_request {
   bool json = true;
   forge::chain::protocol::account_name code;
   forge::chain::protocol::name scope;
   forge::chain::protocol::name table;
   std::string table_key;
   std::string lower_bound;
   std::string upper_bound;
   std::string index_position;
   std::string key_type;
   std::string encode_type;
   std::uint32_t limit = 10;
   bool reverse = false;
   bool show_payer = false;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const table_rows_request&) const = default;
};

struct table_rows_response : audited_response {
   std::vector<forge::variant> rows;
   bool more = false;
   std::string next_key;

   bool operator==(const table_rows_response&) const = default;
};

struct table_scope_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::name table;
   std::string lower_bound;
   std::string upper_bound;
   std::uint32_t limit = 10;
   bool reverse = false;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const table_scope_request&) const = default;
};

struct table_scope_row {
   forge::chain::protocol::name code;
   forge::chain::protocol::name scope;
   forge::chain::protocol::name table;
   forge::chain::protocol::account_name payer;
   std::uint32_t count = 0;

   bool operator==(const table_scope_row&) const = default;
};

struct table_scope_response : audited_response {
   std::vector<table_scope_row> rows;
   bool more = false;
   std::string next_key;

   bool operator==(const table_scope_response&) const = default;
};

struct currency_balance_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::account_name account;
   std::optional<forge::chain::protocol::symbol_code> symbol;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const currency_balance_request&) const = default;
};

struct currency_stats_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::symbol_code symbol{};
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const currency_stats_request&) const = default;
};

struct currency_balance_response : audited_response {
   std::vector<asset> balances;

   bool operator==(const currency_balance_response&) const = default;
};

struct currency_stats_response : audited_response {
   forge::variant stats;

   bool operator==(const currency_stats_response&) const = default;
};

struct scheduled_request {
   std::string lower_bound;
   std::uint32_t limit = 50;
   bool json = true;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const scheduled_request&) const = default;
};

struct scheduled_response : audited_response {
   std::vector<forge::variant> transactions;
   bool more = false;
   std::string next;

   bool operator==(const scheduled_response&) const = default;
};

struct authorizers_request {
   std::vector<forge::chain::protocol::account_name> accounts;
   std::vector<forge::chain::protocol::public_key> keys;
   std::optional<block_id> anchor;
   audit_mode audit = audit_mode::none;

   bool operator==(const authorizers_request&) const = default;
};

struct authorizers_response : audited_response {
   std::vector<permission_level> authorizers;

   bool operator==(const authorizers_response&) const = default;
};

BOOST_DESCRIBE_STRUCT(key_range, (), (lower, upper))
BOOST_DESCRIBE_STRUCT(state_point_request, (), (key, anchor, audit))
BOOST_DESCRIBE_STRUCT(state_range_request, (), (range, anchor, limit, audit))
BOOST_DESCRIBE_STRUCT(state_range_item, (), (key, value))
BOOST_DESCRIBE_STRUCT(state_point_response, (audited_response), (value))
BOOST_DESCRIBE_STRUCT(state_range_response, (audited_response), (rows, next_key))
BOOST_DESCRIBE_STRUCT(state_mutation, (), (key, value))
BOOST_DESCRIBE_STRUCT(state_change_range, (), (range, mutations, next_key))
BOOST_DESCRIBE_STRUCT(state_change_batch, (), (anchor, ranges))
BOOST_DESCRIBE_STRUCT(state_changes_cursor, (), (block, range, key))
BOOST_DESCRIBE_STRUCT(state_changes_request, (), (from_block, to_block, ranges, limit, cursor, audit))
BOOST_DESCRIBE_STRUCT(state_changes_response, (audited_response), (blocks, next))
BOOST_DESCRIBE_STRUCT(account_request, (), (account, anchor, audit))
BOOST_DESCRIBE_STRUCT(account_response, (audited_response), (account, data))
BOOST_DESCRIBE_STRUCT(code_request, (), (account, include_code, include_abi, known_abi_hash, anchor, audit))
BOOST_DESCRIBE_STRUCT(code_response, (audited_response), (account, hash, abi_hash, wasm, raw_abi, abi))
BOOST_DESCRIBE_STRUCT(table_rows_request, (),
                      (json, code, scope, table, table_key, lower_bound, upper_bound, index_position, key_type,
                       encode_type, limit, reverse, show_payer, anchor, audit))
BOOST_DESCRIBE_STRUCT(table_rows_response, (audited_response), (rows, more, next_key))
BOOST_DESCRIBE_STRUCT(table_scope_request, (), (code, table, lower_bound, upper_bound, limit, reverse, anchor, audit))
BOOST_DESCRIBE_STRUCT(table_scope_row, (), (code, scope, table, payer, count))
BOOST_DESCRIBE_STRUCT(table_scope_response, (audited_response), (rows, more, next_key))
BOOST_DESCRIBE_STRUCT(currency_balance_request, (), (code, account, symbol, anchor, audit))
BOOST_DESCRIBE_STRUCT(currency_stats_request, (), (code, symbol, anchor, audit))
BOOST_DESCRIBE_STRUCT(currency_balance_response, (audited_response), (balances))
BOOST_DESCRIBE_STRUCT(currency_stats_response, (audited_response), (stats))
BOOST_DESCRIBE_STRUCT(scheduled_request, (), (lower_bound, limit, json, anchor, audit))
BOOST_DESCRIBE_STRUCT(scheduled_response, (audited_response), (transactions, more, next))
BOOST_DESCRIBE_STRUCT(authorizers_request, (), (accounts, keys, anchor, audit))
BOOST_DESCRIBE_STRUCT(authorizers_response, (audited_response), (authorizers))

} // namespace forge::chain::protocol

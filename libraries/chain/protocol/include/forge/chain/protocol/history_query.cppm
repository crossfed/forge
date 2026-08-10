module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <vector>

export module forge.chain.protocol.history_query;

import forge.variant.containers;
import forge.variant.described;
export import forge.chain.protocol.audit;
export import forge.chain.protocol.block_query;
export import forge.chain.protocol.transaction_query;

export namespace forge::chain::protocol {

struct transaction_history_request {
   transaction_id id;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const transaction_history_request&) const = default;
};

struct history_location {
   block_id block;
   std::uint32_t block_num = 0;
   time_point block_time{};
   bool canonical = false;

   bool operator==(const history_location&) const = default;
};

struct transaction_lookup_response : transaction_status_response {
   packed_transaction transaction;
};

struct transaction_trace_response : audited_response {
   history_location location;
   transaction_trace trace;

   bool operator==(const transaction_trace_response&) const = default;
};

struct block_traces_response : audited_response {
   history_location location;
   std::vector<transaction_trace> traces;

   bool operator==(const block_traces_response&) const = default;
};

struct account_actions_request {
   account_name account;
   std::optional<bytes> cursor;
   std::uint32_t limit = 100;
   bool reverse = false;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const account_actions_request&) const = default;
};

struct account_action_record {
   transaction_id transaction;
   history_location location;
   action_trace trace;

   bool operator==(const account_action_record&) const = default;
};

struct account_actions_response : audited_response {
   std::vector<account_action_record> actions;
   std::optional<bytes> next;

   bool operator==(const account_actions_response&) const = default;
};

BOOST_DESCRIBE_STRUCT(transaction_history_request, (), (id, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(history_location, (), (block, block_num, block_time, canonical))
BOOST_DESCRIBE_STRUCT(transaction_lookup_response, (transaction_status_response), (transaction))
BOOST_DESCRIBE_STRUCT(transaction_trace_response, (audited_response), (location, trace))
BOOST_DESCRIBE_STRUCT(block_traces_response, (audited_response), (location, traces))
BOOST_DESCRIBE_STRUCT(account_actions_request, (), (account, cursor, limit, reverse, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(account_action_record, (), (transaction, location, trace))
BOOST_DESCRIBE_STRUCT(account_actions_response, (audited_response), (actions, next))

} // namespace forge::chain::protocol

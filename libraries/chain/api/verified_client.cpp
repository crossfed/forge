module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <coroutine>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module forge.chain.api.verified_client;

import forge.chain.api.exceptions;
import forge.crypto.digest.sha256;

namespace forge::chain::api {
namespace {

template <typename Response> boost::asio::awaitable<Response> unsupported_audit(const char* method) {
   FORGE_THROW_EXCEPTION(exceptions::audit_not_supported, "verified chain API method has no content proof verifier",
                         forge::exceptions::ctx("method", method));
   co_return Response{};
}

[[noreturn]] void unsupported_projection(const char* method) {
   FORGE_THROW_EXCEPTION(exceptions::audit_not_supported,
                         "verified chain API method has no deterministic projection verifier",
                         forge::exceptions::ctx("method", method));
}

projection_verifier& require_projection(const std::shared_ptr<projection_verifier>& value, const char* method) {
   if (!value) {
      unsupported_projection(method);
   }
   return *value;
}

} // namespace

const protocol::bytes& require_content_witness(const protocol::audit_bundle& audit, protocol::digest expected,
                                               std::optional<std::uint64_t> expected_size) {
   const protocol::content_witness* match = nullptr;
   for (const auto& witness : audit.content) {
      if (witness.hash != expected) {
         continue;
      }
      if (match) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API audit contains duplicate content witnesses");
      }
      match = &witness;
   }

   if (!match) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API audit omits the required content witness");
   }
   if (expected_size && *expected_size != match->value.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API content witness has an unexpected size");
   }
   if (forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{match->value}) != expected) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API content witness does not match its digest");
   }
   return match->value;
}

projection_verifier::~projection_verifier() = default;

void projection_verifier::verify(const protocol::block_request&, const protocol::block_state_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("block.get_block_state");
}

void projection_verifier::verify(const protocol::block_range_request&, const protocol::block_range_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("block.get_canonical_range");
}

void projection_verifier::verify(const protocol::protocol_features_request&,
                                 const protocol::protocol_features_response&, const protocol::audit_bundle&,
                                 audit_verifier&) {
   unsupported_projection("block.get_activated_protocol_features");
}

void projection_verifier::verify(const protocol::anchored_request&, const protocol::consensus_parameters_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("block.get_consensus_parameters");
}

void projection_verifier::verify(const protocol::producers_request&, const protocol::producers_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("block.get_producers");
}

void projection_verifier::verify(const protocol::anchored_request&, const protocol::producer_schedule_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("block.get_producer_schedule");
}

void projection_verifier::verify(const protocol::anchored_request&, const protocol::finalizer_info_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("block.get_finalizer_info");
}

void projection_verifier::verify(const protocol::account_request&, const protocol::account_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_account");
}

void projection_verifier::verify(const protocol::code_request&, const protocol::code_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_code");
}

void projection_verifier::verify(const protocol::table_rows_request&, const protocol::table_rows_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_table_rows");
}

void projection_verifier::verify(const protocol::table_scope_request&, const protocol::table_scope_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_table_scope");
}

void projection_verifier::verify(const protocol::currency_balance_request&, const protocol::currency_balance_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_currency_balance");
}

void projection_verifier::verify(const protocol::currency_stats_request&, const protocol::currency_stats_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_currency_stats");
}

void projection_verifier::verify(const protocol::scheduled_request&, const protocol::scheduled_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_scheduled_transactions");
}

void projection_verifier::verify(const protocol::authorizers_request&, const protocol::authorizers_response&,
                                 const protocol::audit_bundle&, audit_verifier&) {
   unsupported_projection("state.get_accounts_by_authorizers");
}

verified_client::verified_client(raw_client client, std::shared_ptr<audit_verifier> verifier,
                                 std::shared_ptr<projection_verifier> projections)
    : client_{std::move(client)}, verifier_{std::move(verifier)}, projections_{std::move(projections)} {
   if (!verifier_) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "verified chain API client requires a trust verifier");
   }
}

const protocol::audit_bundle& verified_client::verify_envelope(const protocol::audited_response& response) {
   verifier_->verify_context(response.context);
   if (!response.context.anchor || !response.audit) {
      FORGE_THROW_EXCEPTION(exceptions::audit_not_supported, "chain API response omits required audit data");
   }
   if (!response.audit->finality) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality, "chain API response omits its finality proof");
   }
   verifier_->verify_finality(*response.context.anchor, *response.audit->finality);
   return *response.audit;
}

void verified_client::verify_requested_anchor(const std::optional<protocol::block_id>& requested,
                                              const protocol::audited_response& response) {
   if (requested && response.context.anchor->block != *requested) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "chain API response does not match the requested anchor block");
   }
}

void verified_client::verify_point(const protocol::state_point_request& request,
                                   const protocol::state_point_response& response) {
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(request.anchor, response);
   if (audit.state.size() != 1U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                            "chain API point response requires exactly one state proof");
   }
   if (verifier_->verify_state_point(*response.context.anchor, request, audit.state.front()) != response.value) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                            "chain API point value does not match its authenticated proof");
   }
}

void verified_client::verify_range(const protocol::state_range_request& request,
                                   const protocol::state_range_response& response) {
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(request.anchor, response);
   if (audit.state.size() != 1U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                            "chain API range response requires exactly one state proof");
   }
   const auto verified = verifier_->verify_state_range(*response.context.anchor, request, audit.state.front());
   if (verified.rows != response.rows || verified.next_key != response.next_key) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                            "chain API range result does not match its authenticated proof");
   }
}

void verified_client::verify_changes(const protocol::state_changes_request& request,
                                     const protocol::state_changes_response& response) {
   const auto& audit = verify_envelope(response);
   const auto reject = [](const char* message) { FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, message); };
   const auto range_count = request.ranges.empty() ? std::size_t{1} : request.ranges.size();
   if (request.from_block > request.to_block || request.limit == 0U ||
       response.context.anchor->block_num != request.to_block) {
      reject("chain API changes request or finalized target anchor is invalid");
   }
   if (request.from_block == request.to_block) {
      if (request.cursor || !response.blocks.empty() || response.next || !audit.state.empty()) {
         reject("chain API empty changes interval contains data or a cursor");
      }
      return;
   }

   auto position = request.cursor.value_or(protocol::state_changes_cursor{
       .block = request.from_block + 1U,
   });
   if (position.block <= request.from_block || position.block > request.to_block || position.range >= range_count) {
      reject("chain API changes cursor is outside the requested interval");
   }
   const auto original_range = [&](std::size_t index) -> const protocol::key_range& {
      static const auto unbounded = protocol::key_range{};
      return request.ranges.empty() ? unbounded : request.ranges[index];
   };
   const auto key_less = [](const protocol::bytes& left, const protocol::bytes& right) {
      return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
   };
   const auto valid_cursor_key = [&](const protocol::key_range& range, const std::optional<protocol::bytes>& key) {
      return !key ||
             ((!range.lower || !key_less(*key, *range.lower)) && (!range.upper || key_less(*key, *range.upper)));
   };
   if (!valid_cursor_key(original_range(position.range), position.key)) {
      reject("chain API changes cursor key is outside its requested range");
   }

   auto expected_proofs = std::size_t{};
   for (const auto& batch : response.blocks) {
      expected_proofs += batch.ranges.size();
   }
   if (audit.state.size() != expected_proofs) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                            "chain API changes response requires one proof per block range");
   }
   auto ancestry = std::vector<protocol::state_anchor>{};
   ancestry.reserve(response.blocks.size());
   for (const auto& batch : response.blocks) {
      if (batch.anchor != *response.context.anchor) {
         ancestry.push_back(batch.anchor);
      }
   }
   if (!ancestry.empty()) {
      if (!audit.ancestry) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                               "chain API changes response omits its canonical ancestry proof");
      }
      verifier_->verify_ancestry(*response.context.anchor, ancestry, *audit.ancestry);
   }
   auto proof_index = std::size_t{};
   auto stopped_within_range = false;
   auto complete = false;
   for (const auto& batch : response.blocks) {
      if (complete || stopped_within_range || batch.anchor.chain != response.context.chain ||
          batch.anchor.block_num != position.block || batch.ranges.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                               "chain API changes response contains a non-canonical block sequence");
      }
      for (const auto& result : batch.ranges) {
         const auto& expected = original_range(position.range);
         if (result.range != expected) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                                  "chain API changes response reordered its requested ranges");
         }
         auto proof_range = expected;
         if (position.key) {
            proof_range.lower = position.key;
         }
         const auto verified =
             verifier_->verify_state_changes(batch.anchor, proof_range, request.limit, audit.state[proof_index++]);
         if (verified != result) {
            reject("chain API change range does not match its authenticated proof");
         }
         if (result.next_key) {
            if (!valid_cursor_key(expected, result.next_key) ||
                (proof_range.lower && !key_less(*proof_range.lower, *result.next_key))) {
               reject("chain API changes response has an invalid continuation key");
            }
            position.key = result.next_key;
            stopped_within_range = true;
            break;
         }
         position.key.reset();
         ++position.range;
         if (position.range == range_count) {
            position.range = 0;
            if (position.block == request.to_block) {
               complete = true;
            } else {
               ++position.block;
            }
         }
      }
   }

   if (response.next) {
      if (complete || *response.next != position) {
         reject("chain API changes response cursor does not match the verified continuation");
      }
   } else if (!complete || position.range != 0U || position.key || response.blocks.empty() ||
              response.blocks.back().anchor != *response.context.anchor) {
      reject("chain API changes response does not reach its finalized target anchor");
   }
}

void verified_client::verify_transaction_status(const forge::chain::protocol::transaction_id& expected,
                                                const protocol::transaction_status_response& response) {
   const auto& audit = verify_envelope(response);
   if (response.head != response.context.head || response.finalized != response.context.finalized ||
       response.head_num != protocol::calculate_block_num_from_id(response.head) ||
       response.finalized_num != protocol::calculate_block_num_from_id(response.finalized)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "chain API transaction payload is inconsistent with its audited context");
   }
   if (response.id != expected) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                            "chain API transaction response has the wrong transaction id");
   }
   if (response.state != protocol::transaction_lifecycle::included &&
       response.state != protocol::transaction_lifecycle::finalized) {
      FORGE_THROW_EXCEPTION(exceptions::audit_not_supported,
                            "chain API cannot prove a transaction before canonical inclusion");
   }
   if (!audit.transaction) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                            "chain API response omits its transaction inclusion proof");
   }
   verifier_->verify_transaction(*response.context.anchor, expected, response, *audit.transaction);
}

boost::asio::awaitable<protocol::info_response> verified_client::get_info() {
   return get_info(protocol::anchored_request{});
}

boost::asio::awaitable<protocol::info_response> verified_client::get_info(protocol::anchored_request request) {
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.info().get(std::move(request));
   static_cast<void>(verify_envelope(response));
   verify_requested_anchor(requested_anchor, response);
   if (response.chain != response.context.chain) {
      FORGE_THROW_EXCEPTION(exceptions::wrong_chain, "chain API info payload belongs to another chain");
   }
   if (response.head != response.context.head || response.finalized != response.context.finalized ||
       response.head_num != protocol::calculate_block_num_from_id(response.head) ||
       response.finalized_num != protocol::calculate_block_num_from_id(response.finalized) ||
       response.context.anchor->block != response.finalized ||
       response.context.anchor->block_num != response.finalized_num) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "chain API info payload is inconsistent with its audited context");
   }
   co_return response;
}

boost::asio::awaitable<protocol::block_response> verified_client::get_block(protocol::block_request request) {
   request.audit = protocol::audit_mode::required;
   const auto requested_id = request.id;
   const auto requested_num = request.num;
   auto response = co_await client_.blocks().get_block(std::move(request));
   static_cast<void>(verify_envelope(response));
   if ((requested_id && response.id != *requested_id) || (requested_num && response.num != *requested_num) ||
       response.id != response.context.anchor->block || response.num != response.context.anchor->block_num ||
       response.block.calculate_id() != response.id || response.block.calculate_block_num() != response.num ||
       protocol::calculate_transaction_mroot(response.block.transactions) != response.block.transaction_mroot ||
       response.block.transaction_mroot != response.context.anchor->transaction_root || !response.canonical) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "chain API block response does not match its request and audited anchor");
   }
   co_return response;
}

boost::asio::awaitable<protocol::block_header_response> verified_client::get_header(protocol::block_request request) {
   request.audit = protocol::audit_mode::required;
   const auto requested_id = request.id;
   const auto requested_num = request.num;
   auto response = co_await client_.blocks().get_header(std::move(request));
   static_cast<void>(verify_envelope(response));
   if ((requested_id && response.id != *requested_id) || (requested_num && response.num != *requested_num) ||
       response.id != response.context.anchor->block || response.num != response.context.anchor->block_num ||
       response.header.calculate_id() != response.id || response.header.calculate_block_num() != response.num ||
       response.header.transaction_mroot != response.context.anchor->transaction_root || !response.canonical) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "chain API block header does not match its request and audited anchor");
   }
   co_return response;
}

boost::asio::awaitable<protocol::block_state_response>
verified_client::get_block_state(protocol::block_request request) {
   auto& projections = require_projection(projections_, "block.get_block_state");
   request.audit = protocol::audit_mode::required;
   auto response = co_await client_.blocks().get_block_state(request);
   const auto& audit = verify_envelope(response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::block_range_response>
verified_client::get_canonical_range(protocol::block_range_request request) {
   auto& projections = require_projection(projections_, "block.get_canonical_range");
   request.audit = protocol::audit_mode::required;
   auto response = co_await client_.blocks().get_canonical_range(request);
   const auto& audit = verify_envelope(response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::protocol_features_response>
verified_client::get_activated_protocol_features(protocol::protocol_features_request request) {
   auto& projections = require_projection(projections_, "block.get_activated_protocol_features");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.blocks().get_activated_protocol_features(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::consensus_parameters_response>
verified_client::get_consensus_parameters(protocol::anchored_request request) {
   auto& projections = require_projection(projections_, "block.get_consensus_parameters");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.blocks().get_consensus_parameters(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::producers_response>
verified_client::get_producers(protocol::producers_request request) {
   auto& projections = require_projection(projections_, "block.get_producers");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.blocks().get_producers(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::producer_schedule_response>
verified_client::get_producer_schedule(protocol::anchored_request request) {
   auto& projections = require_projection(projections_, "block.get_producer_schedule");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.blocks().get_producer_schedule(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::finalizer_info_response>
verified_client::get_finalizer_info(protocol::anchored_request request) {
   auto& projections = require_projection(projections_, "block.get_finalizer_info");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.blocks().get_finalizer_info(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::state_point_response>
verified_client::get_point(protocol::state_point_request request) {
   request.audit = protocol::audit_mode::required;
   auto response = co_await client_.state().get_point(request);
   verify_point(request, response);
   co_return response;
}

boost::asio::awaitable<protocol::state_range_response>
verified_client::get_range(protocol::state_range_request request) {
   request.audit = protocol::audit_mode::required;
   auto response = co_await client_.state().get_range(request);
   verify_range(request, response);
   co_return response;
}

boost::asio::awaitable<protocol::state_changes_response>
verified_client::get_changes(protocol::state_changes_request request) {
   request.audit = protocol::audit_mode::required;
   auto response = co_await client_.state().get_changes(request);
   verify_changes(request, response);
   co_return response;
}

boost::asio::awaitable<protocol::account_response> verified_client::get_account(protocol::account_request request) {
   auto& projections = require_projection(projections_, "state.get_account");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_account(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::code_response> verified_client::get_code(protocol::code_request request) {
   auto& projections = require_projection(projections_, "state.get_code");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_code(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::table_rows_response>
verified_client::get_table_rows(protocol::table_rows_request request) {
   auto& projections = require_projection(projections_, "state.get_table_rows");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_table_rows(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::table_scope_response>
verified_client::get_table_scope(protocol::table_scope_request request) {
   auto& projections = require_projection(projections_, "state.get_table_scope");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_table_scope(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::currency_balance_response>
verified_client::get_currency_balance(protocol::currency_balance_request request) {
   auto& projections = require_projection(projections_, "state.get_currency_balance");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_currency_balance(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::currency_stats_response>
verified_client::get_currency_stats(protocol::currency_stats_request request) {
   auto& projections = require_projection(projections_, "state.get_currency_stats");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_currency_stats(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::scheduled_response>
verified_client::get_scheduled_transactions(protocol::scheduled_request request) {
   auto& projections = require_projection(projections_, "state.get_scheduled_transactions");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_scheduled_transactions(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::authorizers_response>
verified_client::get_accounts_by_authorizers(protocol::authorizers_request request) {
   auto& projections = require_projection(projections_, "state.get_accounts_by_authorizers");
   request.audit = protocol::audit_mode::required;
   const auto requested_anchor = request.anchor;
   auto response = co_await client_.state().get_accounts_by_authorizers(request);
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(requested_anchor, response);
   projections.verify(request, response, audit, *verifier_);
   co_return response;
}

boost::asio::awaitable<protocol::transaction_status_response>
verified_client::get_transaction_status(protocol::transaction_status_request request) {
   request.audit = protocol::audit_mode::required;
   const auto expected = request.id;
   auto response = co_await client_.transactions().get_status(std::move(request));
   verify_transaction_status(expected, response);
   co_return response;
}

boost::asio::awaitable<protocol::transaction_status_response>
verified_client::await_transaction(protocol::transaction_await_request request) {
   request.audit = protocol::audit_mode::required;
   const auto expected = request.id;
   const auto desired = request.desired;
   auto response = co_await client_.transactions().await_transaction(std::move(request));
   verify_transaction_status(expected, response);
   if ((desired == protocol::transaction_lifecycle::finalized &&
        response.state != protocol::transaction_lifecycle::finalized) ||
       (desired != protocol::transaction_lifecycle::included &&
        desired != protocol::transaction_lifecycle::finalized)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                            "chain API transaction response does not satisfy the requested verified lifecycle");
   }
   co_return response;
}

boost::asio::awaitable<std::vector<protocol::public_key>>
verified_client::get_required_keys(protocol::transaction_required_keys_request) {
   return unsupported_audit<std::vector<protocol::public_key>>("transaction.get_required_keys");
}

boost::asio::awaitable<protocol::transaction_read_only_response>
verified_client::compute_transaction(protocol::transaction_read_only_request) {
   return unsupported_audit<protocol::transaction_read_only_response>("transaction.compute_transaction");
}

boost::asio::awaitable<protocol::transaction_read_only_response>
verified_client::send_read_only_transaction(protocol::transaction_read_only_request) {
   return unsupported_audit<protocol::transaction_read_only_response>("transaction.send_read_only_transaction");
}

raw_client& verified_client::raw() noexcept {
   return client_;
}

} // namespace forge::chain::api

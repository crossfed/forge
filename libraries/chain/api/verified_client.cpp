module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <coroutine>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

module forge.chain.api.verified_client;

import forge.chain.api.exceptions;

namespace forge::chain::api {

verified_client::verified_client(raw_client client, std::shared_ptr<audit_verifier> verifier)
    : client_{std::move(client)}, verifier_{std::move(verifier)} {
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
   verifier_->verify_state_point(*response.context.anchor, request, response.value, audit.state.front());
}

void verified_client::verify_range(const protocol::state_range_request& request,
                                   const protocol::state_range_response& response) {
   const auto& audit = verify_envelope(response);
   verify_requested_anchor(request.anchor, response);
   if (audit.state.size() != 1U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof,
                            "chain API range response requires exactly one state proof");
   }
   verifier_->verify_state_range(*response.context.anchor, request, response, audit.state.front());
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
         verifier_->verify_state_changes(batch.anchor, proof_range, request.limit, result, audit.state[proof_index++]);
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
   auto response = co_await client_.info().get({.audit = protocol::audit_mode::required});
   static_cast<void>(verify_envelope(response));
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

boost::asio::awaitable<protocol::block_response> verified_client::get_block(protocol::block_request request) {
   request.audit = protocol::audit_mode::required;
   const auto requested_id = request.id;
   const auto requested_num = request.num;
   auto response = co_await client_.blocks().get_block(std::move(request));
   static_cast<void>(verify_envelope(response));
   if ((requested_id && response.id != *requested_id) || (requested_num && response.num != *requested_num) ||
       response.id != response.context.anchor->block || response.num != response.context.anchor->block_num ||
       response.block.calculate_id() != response.id || response.block.calculate_block_num() != response.num) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "chain API block response does not match its request and audited anchor");
   }
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
   auto response = co_await client_.transactions().await_transaction(std::move(request));
   verify_transaction_status(expected, response);
   co_return response;
}

raw_client& verified_client::raw() noexcept {
   return client_;
}

} // namespace forge::chain::api

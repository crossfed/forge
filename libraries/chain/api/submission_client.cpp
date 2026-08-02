module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <utility>
#include <vector>

module forge.chain.api.submission_client;

import forge.chain.api.exceptions;

namespace forge::chain::api {
namespace {

transaction& require_service(
   const forge::api::core::handle<transaction>& value) {
   if (!value) {
      FORGE_THROW_EXCEPTION(
         exceptions::unavailable,
         "chain transaction submission service is unavailable");
   }
   return *value.shared();
}

void verify_acknowledgement(
   const protocol::transaction_submit_response& response,
   const protocol::transaction_id& expected,
   const char* message) {
   if (response.id != expected ||
       (response.trace && response.trace->id != expected)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof, message);
   }
}

} // namespace

submission_client::submission_client(
   forge::api::core::handle<transaction> service)
    : service_{std::move(service)} {}

boost::asio::awaitable<protocol::transaction_submit_response>
submission_client::submit(protocol::transaction_submit_request request) {
   auto& service = require_service(service_);
   const auto expected = request.transaction.id();
   auto response = co_await service.submit(std::move(request));
   verify_acknowledgement(
      response,
      expected,
      "chain API submit acknowledgement does not match the submitted transaction");
   co_return response;
}

boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
submission_client::submit_batch(
   std::vector<protocol::transaction_submit_request> requests) {
   auto& service = require_service(service_);
   auto expected = std::vector<protocol::transaction_id>{};
   expected.reserve(requests.size());
   for (const auto& request : requests) {
      expected.push_back(request.transaction.id());
   }

   auto responses = co_await service.submit_batch(std::move(requests));
   if (responses.size() != expected.size()) {
      FORGE_THROW_EXCEPTION(
         exceptions::invalid_transaction_proof,
         "chain API submit acknowledgement count does not match the request");
   }
   for (auto index = std::size_t{}; index < responses.size(); ++index) {
      verify_acknowledgement(
         responses[index],
         expected[index],
         "chain API submit acknowledgement does not match transaction order");
   }
   co_return responses;
}

} // namespace forge::chain::api

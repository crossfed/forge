module;

#include <boost/asio/awaitable.hpp>

#include <vector>

export module forge.chain.api.submission_client;

export import forge.chain.api.transaction;

import forge.api.core.handle;

export namespace forge::chain::api {

// Submission acknowledges transport acceptance only. Finality remains a
// separate verified_client operation over the returned transaction id.
class submission_client {
 public:
   explicit submission_client(forge::api::core::handle<transaction> service);

   boost::asio::awaitable<protocol::transaction_submit_response>
   submit(protocol::transaction_submit_request request);
   boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
   submit_batch(std::vector<protocol::transaction_submit_request> requests);

 private:
   forge::api::core::handle<transaction> service_;
};

} // namespace forge::chain::api

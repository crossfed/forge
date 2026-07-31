module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>

export module forge.chain.api.verified_client;

export import forge.chain.api.raw_client;

export namespace forge::chain::api {

class audit_verifier {
 public:
   virtual ~audit_verifier() = default;

   virtual void verify_context(const protocol::response_context& context) = 0;
   virtual void verify_finality(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) = 0;
   virtual void verify_state_point(const protocol::state_anchor& anchor, const protocol::state_point_request& request,
                                   const std::optional<protocol::bytes>& value, const protocol::proof_blob& proof) = 0;
   virtual void verify_state_range(const protocol::state_anchor& anchor, const protocol::state_range_request& request,
                                   const protocol::state_range_response& response,
                                   const protocol::proof_blob& proof) = 0;
   virtual void verify_state_changes(const protocol::state_anchor& anchor, const protocol::key_range& range,
                                     std::uint32_t limit, const protocol::state_change_range& result,
                                     const protocol::proof_blob& proof) = 0;
   virtual void verify_transaction(const protocol::state_anchor& anchor,
                                   const forge::chain::protocol::transaction_id& expected,
                                   const protocol::transaction_status_response& response,
                                   const protocol::transaction_inclusion_proof& proof) = 0;
};

class verified_client {
 public:
   verified_client(raw_client client, std::shared_ptr<audit_verifier> verifier);

   boost::asio::awaitable<protocol::info_response> get_info();
   boost::asio::awaitable<protocol::state_point_response> get_point(protocol::state_point_request request);
   boost::asio::awaitable<protocol::state_range_response> get_range(protocol::state_range_request request);
   boost::asio::awaitable<protocol::state_changes_response> get_changes(protocol::state_changes_request request);
   boost::asio::awaitable<protocol::block_response> get_block(protocol::block_request request);
   boost::asio::awaitable<protocol::transaction_status_response>
   get_transaction_status(protocol::transaction_status_request request);
   boost::asio::awaitable<protocol::transaction_status_response>
   await_transaction(protocol::transaction_await_request request);

   [[nodiscard]] raw_client& raw() noexcept;

 private:
   const protocol::audit_bundle& verify_envelope(const protocol::audited_response& response);
   void verify_requested_anchor(const std::optional<protocol::block_id>& requested,
                                const protocol::audited_response& response);
   void verify_point(const protocol::state_point_request& request, const protocol::state_point_response& response);
   void verify_range(const protocol::state_range_request& request, const protocol::state_range_response& response);
   void verify_changes(const protocol::state_changes_request& request,
                       const protocol::state_changes_response& response);
   void verify_transaction_status(const forge::chain::protocol::transaction_id& expected,
                                  const protocol::transaction_status_response& response);

   raw_client client_;
   std::shared_ptr<audit_verifier> verifier_;
};

} // namespace forge::chain::api

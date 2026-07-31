module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

export module forge.chain.api.authenticated_audit_verifier;

export import forge.chain.api.verified_client;
export import forge.db.authenticated.types;

export namespace forge::chain::api {

class finality_verifier {
 public:
   virtual ~finality_verifier() = default;

   virtual void verify(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) = 0;
};

struct authenticated_audit_options {
   protocol::chain_id chain;
   std::string state_domain;
   forge::db::authenticated::limits proof_limits;
};

class authenticated_audit_verifier final : public audit_verifier {
 public:
   authenticated_audit_verifier(authenticated_audit_options options,
                                std::shared_ptr<finality_verifier> finality);

   void verify_context(const protocol::response_context& context) override;
   void verify_finality(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) override;
   void verify_state_point(const protocol::state_anchor& anchor, const protocol::state_point_request& request,
                           const std::optional<protocol::bytes>& value,
                           const protocol::proof_blob& proof) override;
   void verify_state_range(const protocol::state_anchor& anchor, const protocol::state_range_request& request,
                           const protocol::state_range_response& response,
                           const protocol::proof_blob& proof) override;
   void verify_state_changes(const protocol::state_anchor& anchor, const protocol::key_range& range,
                             std::uint32_t limit, const protocol::state_change_range& result,
                             const protocol::proof_blob& proof) override;
   void verify_transaction(const protocol::state_anchor& anchor, const protocol::transaction_id& expected,
                           const protocol::transaction_status_response& response,
                           const protocol::transaction_inclusion_proof& proof) override;

 private:
   authenticated_audit_options options_;
   std::shared_ptr<finality_verifier> finality_;
};

} // namespace forge::chain::api

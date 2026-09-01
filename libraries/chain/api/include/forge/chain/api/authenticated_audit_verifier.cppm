module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

export module forge.chain.api.authenticated_audit_verifier;

export import forge.chain.api.finality;
export import forge.chain.api.verified_client;
export import forge.db.authenticated.types;

export namespace forge::chain::api {

struct authenticated_audit_options {
   protocol::chain_id chain;
   std::string state_domain;
   forge::db::authenticated::limits proof_limits;
};

class authenticated_audit_verifier final : public audit_verifier {
 public:
   authenticated_audit_verifier(authenticated_audit_options options, std::shared_ptr<finality_verifier> finality);

   [[nodiscard]] std::optional<protocol::block_id> preferred_finality_anchor() const override;
   [[nodiscard]] std::optional<protocol::block_id>
   finality_anchor_at_or_before(protocol::block_num target) const override;

   void verify_context(const protocol::response_context& context) override;
   void verify_finality(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) override;
   std::optional<protocol::bytes> verify_state_point(const protocol::state_anchor& anchor, const protocol::bytes& key,
                                                     const protocol::proof_blob& proof) override;
   authenticated_state_range verify_state_range(const protocol::state_anchor& anchor,
                                                const authenticated_range_query& request,
                                                const protocol::proof_blob& proof) override;
   authenticated_change_range verify_state_changes(const protocol::state_anchor& anchor,
                                                   const authenticated_range_query& request,
                                                   const protocol::proof_blob& proof) override;
   void verify_ancestry(const protocol::state_anchor& finalized, std::span<const protocol::state_anchor> intermediate,
                        const protocol::proof_blob& proof) override;
   void verify_transaction(const protocol::state_anchor& anchor, const protocol::transaction_id& expected,
                           const protocol::transaction_status_response& response,
                           const protocol::transaction_inclusion_proof& proof) override;

 private:
   authenticated_audit_options options_;
   std::shared_ptr<finality_verifier> finality_;
};

} // namespace forge::chain::api

module;

#include <memory>
#include <string>
#include <vector>

export module forge.chain.api.verified_client_factory;

export import forge.chain.api.authenticated_audit_verifier;
export import forge.chain.api.savanna_finality_verifier;
export import forge.chain.api.verified_client;

export namespace forge::chain::api {

struct verified_client_options {
   protocol::chain_id chain;
   std::string state_domain;
   savanna::finality_trust trust;
   forge::db::authenticated::limits proof_limits = {};
   protocol::service_limits service_limits = {};
   std::shared_ptr<projection_verifier> projections;
   std::vector<savanna::finality_trust> additional_trusts = {};
};

[[nodiscard]] verified_client make_verified_client(raw_client client, verified_client_options options);

} // namespace forge::chain::api

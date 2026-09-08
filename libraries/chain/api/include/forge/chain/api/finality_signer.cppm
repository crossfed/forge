module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

export module forge.chain.api.finality_signer;

export import forge.chain.savanna.types;
export import forge.chain.savanna.vote;

import forge.crypto.bls;

import forge.api.core.binding;
import forge.api.core.connection;
import forge.api.core.descriptor;
import forge.api.core.dispatcher;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.core.types;

export namespace forge::chain::api {

struct finalizer_identity {
   forge::crypto::bls::public_key public_key;
   forge::crypto::bls::signature proof_of_possession;

   bool operator==(const finalizer_identity&) const = default;
};

class finality_signer : public forge::api::core::contract<finality_signer, forge::api::core::surface::local> {
 public:
   virtual ~finality_signer() = default;

   virtual boost::asio::awaitable<finalizer_identity> identity() = 0;

   virtual boost::asio::awaitable<chain::savanna::finalizer_vote> sign_vote(chain::savanna::block_ref block,
                                                                            chain::savanna::vote_kind kind) = 0;
};

} // namespace forge::chain::api

FORGE_EXPORT_API(::forge::chain::api::finality_signer, FORGE_API_CONTRACT("forge.chain.api.finality_signer", 1, 0),
                 FORGE_API_METHOD(identity), FORGE_API_METHOD(sign_vote, block, kind))

module;

#include <memory>

export module forge.chain.api.savanna_finality_verifier;

export import forge.chain.api.finality;
export import forge.chain.savanna.finality_witness;

export namespace forge::chain::api {

[[nodiscard]] std::shared_ptr<finality_verifier>
make_savanna_finality_verifier(savanna::finality_trust trust, savanna::finality_witness_limits limits = {});

} // namespace forge::chain::api

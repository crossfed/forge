module;

#include <memory>
#include <span>
#include <vector>

export module forge.chain.api.savanna_finality_verifier;

export import forge.chain.api.finality;
export import forge.chain.savanna.finality_witness;

export namespace forge::chain::api {

[[nodiscard]] std::shared_ptr<finality_verifier>
make_savanna_finality_verifier(savanna::finality_trust trust, savanna::finality_witness_limits limits = {});

[[nodiscard]] std::shared_ptr<finality_verifier>
make_savanna_finality_verifier_with_trusts(savanna::finality_trust trust,
                                           std::vector<savanna::finality_trust> additional_trusts,
                                           savanna::finality_witness_limits limits = {});

[[nodiscard]] savanna::header_state replay_savanna_finality_state(
    std::span<const savanna::finality_trust> trusted, const protocol::state_anchor& expected,
    const protocol::proof_blob& proof, savanna::finality_witness_limits limits = {});

} // namespace forge::chain::api

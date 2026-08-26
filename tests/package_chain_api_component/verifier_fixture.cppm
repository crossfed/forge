module;

#include <cstddef>
#include <span>

export module package.chain_api_component.verifier_fixture;

import forge.chain.api.finality;
import forge.chain.protocol.audit;

namespace package_chain_api_component {

class accepting_finality final : public forge::chain::api::finality_verifier {
 public:
   void verify(const forge::chain::protocol::state_anchor& anchor,
               const forge::chain::protocol::proof_blob& proof) override;
   void verify_ancestry(const forge::chain::protocol::state_anchor& anchor,
                        std::span<const forge::chain::protocol::state_anchor> ancestors,
                        const forge::chain::protocol::proof_blob& proof) override;

   std::size_t calls = 0;
};

} // namespace package_chain_api_component

export namespace package_chain_api_component {

void run_verifier_component_checks();

} // namespace package_chain_api_component

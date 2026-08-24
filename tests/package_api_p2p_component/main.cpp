import forge.api.p2p.binding;
import forge.api.p2p.authenticated_peer;
import forge.api.core.server_supplied;
import forge.api.core.trusted_invocation;

int main() {
   auto builder = forge::api::p2p::api();
   auto trusted = forge::api::core::trusted_invocation_builder{}
                      .set(forge::api::p2p::authenticated_peer{})
                      .build();
   auto peer = forge::api::p2p::authenticated_peer{};
   forge::api::core::server_supplied<forge::api::p2p::authenticated_peer>::reset(peer);
   (void)builder;
   return forge::api::core::server_supplied<forge::api::p2p::authenticated_peer>::apply(peer, trusted)
              ? 0
              : 1;
}

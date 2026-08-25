import forge.api.p2p.binding;
import forge.api.p2p.authenticated_peer;
import forge.api.core.server_supplied;
import forge.api.core.trusted_invocation;

struct stream_open_proof {
   forge::net::p2p::peer_id caller;
};

template <> struct forge::api::core::server_supplied<stream_open_proof> {
   static constexpr bool required = true;

   static void reset(stream_open_proof& value) {
      value = {};
   }

   static bool apply(stream_open_proof& value,
                     const forge::api::core::trusted_invocation& trusted) {
      const auto* peer = trusted.find<forge::api::p2p::authenticated_peer>();
      if (peer == nullptr) {
         return false;
      }
      value.caller = peer->id;
      return true;
   }
};

int main() {
   auto builder = forge::api::p2p::api();
   auto trusted = forge::api::core::trusted_invocation_builder{}
                      .set(forge::api::p2p::authenticated_peer{})
                      .build();
   auto proof = stream_open_proof{};
   forge::api::core::server_supplied<stream_open_proof>::reset(proof);
   (void)builder;
   return forge::api::core::server_supplied<stream_open_proof>::apply(proof, trusted)
              ? 0
              : 1;
}

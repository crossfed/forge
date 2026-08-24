export module forge.api.p2p.authenticated_peer;

export import forge.api.core.server_supplied;
export import forge.net.p2p.identity;

export namespace forge::api::p2p {

struct authenticated_peer {
   forge::net::p2p::peer_id id;

   bool operator==(const authenticated_peer&) const = default;
};

} // namespace forge::api::p2p

export namespace forge::api::core {

template <> struct server_supplied<forge::api::p2p::authenticated_peer> {
   static constexpr bool required = true;

   static void reset(forge::api::p2p::authenticated_peer& value) {
      value = {};
   }

   [[nodiscard]] static bool apply(forge::api::p2p::authenticated_peer& value,
                                   const forge::api::core::trusted_invocation& trusted) {
      const auto* peer = trusted.find<forge::api::p2p::authenticated_peer>();
      if (peer == nullptr) {
         return false;
      }
      value = *peer;
      return true;
   }
};

} // namespace forge::api::core

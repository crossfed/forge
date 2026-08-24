export module forge.api.p2p.authenticated_peer;

export import forge.net.p2p.identity;

export namespace forge::api::p2p {

struct authenticated_peer {
   forge::net::p2p::peer_id id;

   bool operator==(const authenticated_peer&) const = default;
};

} // namespace forge::api::p2p

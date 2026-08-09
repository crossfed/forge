#pragma once

namespace forge::plugins::p2p::node::detail {

struct peer_state_schema {
   static constexpr std::uint32_t peer_state_format_version = 1;

   // These IDs are part of the private persisted format and must not be renumbered.
   static constexpr std::uint8_t peer_state_space = 80;
   static constexpr std::uint16_t schema_state_type = 1;
   static constexpr std::uint16_t peer_type = 2;
   static constexpr std::uint16_t provider_type = 3;
   static constexpr std::uint16_t rendezvous_type = 4;

   struct endpoint_fact {
      std::string endpoint;
      std::uint16_t kind = 0;
      std::optional<std::string> relay_peer;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::int64_t last_latency_ms = 0;
      std::int64_t backoff_until_ns = 0;
      double score = 0.0;

      BOOST_DESCRIBE_CLASS(endpoint_fact, (),
                           (endpoint, kind, relay_peer, successes, failures, last_latency_ms, backoff_until_ns, score),
                           (), ())
   };

   struct relay_fact {
      std::string relay;
      std::uint64_t reservation_id = 0;
      std::int64_t expires_at_ns = 0;
      std::vector<std::string> endpoints;
      std::vector<std::uint8_t> voucher;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::int64_t last_latency_ms = 0;
      double score = 0.0;

      BOOST_DESCRIBE_CLASS(relay_fact, (),
                           (relay, reservation_id, expires_at_ns, endpoints, voucher, successes, failures,
                            last_latency_ms, score),
                           (), ())
   };

   struct schema_state : forge::db::object::object<schema_state, peer_state_space, schema_state_type> {
      std::uint32_t format_version = peer_state_format_version;
      std::uint64_t rendezvous_sequence = 0;

      BOOST_DESCRIBE_CLASS(schema_state, (object_base_type), (format_version, rendezvous_sequence), (), ())
   };

   struct peer_row : forge::db::object::object<peer_row, peer_state_space, peer_type> {
      std::string peer;
      std::uint64_t capabilities = 0;
      std::uint16_t discovered_by = 0;
      std::string protocol_version;
      std::string agent_version;
      std::vector<std::uint8_t> public_key;
      std::vector<std::string> protocols;
      std::vector<std::uint8_t> signed_peer_record;
      std::vector<endpoint_fact> endpoints;
      std::vector<relay_fact> relay_reservations;
      std::uint16_t reachability = 0;
      std::optional<std::string> observed_endpoint;
      std::int64_t reachability_expires_at_ns = 0;
      std::int64_t discovered_at_ns = 0;
      std::int64_t discovery_expires_at_ns = 0;
      std::int64_t discovery_backoff_until_ns = 0;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::int64_t last_latency_ms = 0;
      double score = 0.0;
      std::uint64_t hydration_priority = 0;

      BOOST_DESCRIBE_CLASS(peer_row, (object_base_type),
                           (peer, capabilities, discovered_by, protocol_version, agent_version, public_key, protocols,
                            signed_peer_record, endpoints, relay_reservations, reachability, observed_endpoint,
                            reachability_expires_at_ns, discovered_at_ns, discovery_expires_at_ns,
                            discovery_backoff_until_ns, successes, failures, last_latency_ms, score,
                            hydration_priority),
                           (), ())
   };

   struct provider_row : forge::db::object::object<provider_row, peer_state_space, provider_type> {
      std::string key;
      std::string peer;
      std::vector<std::string> endpoints;
      std::uint16_t connection = 0;
      std::uint16_t discovered_by = 0;
      std::int64_t expires_at_ns = 0;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;

      BOOST_DESCRIBE_CLASS(provider_row, (object_base_type),
                           (key, peer, endpoints, connection, discovered_by, expires_at_ns, successes, failures), (),
                           ())
   };

   struct rendezvous_row : forge::db::object::object<rendezvous_row, peer_state_space, rendezvous_type> {
      std::string namespace_name;
      std::string peer;
      std::vector<std::string> endpoints;
      std::vector<std::uint8_t> signed_peer_record;
      std::int64_t ttl_seconds = 0;
      std::int64_t expires_at_ns = 0;
      std::uint64_t sequence = 0;

      BOOST_DESCRIBE_CLASS(rendezvous_row, (object_base_type),
                           (namespace_name, peer, endpoints, signed_peer_record, ttl_seconds, expires_at_ns, sequence),
                           (), ())
   };

   struct by_schema_state_id;
   struct by_peer_row_id;
   struct by_peer_id;
   struct by_peer_hydration;
   struct by_peer_expiry;
   struct by_provider_row_id;
   struct by_provider_key_peer;
   struct by_provider_hydration;
   struct by_provider_expiry;
   struct by_rendezvous_row_id;
   struct by_rendezvous_namespace_peer;
   struct by_rendezvous_sequence;
   struct by_rendezvous_expiry;

   using schema_state_object = forge::db::object::object_index<
       schema_state, forge::db::object::indexed_by<forge::db::object::primary_unique<by_schema_state_id>>>;

   using peer_object = forge::db::object::object_index<
       peer_row,
       forge::db::object::indexed_by<
           forge::db::object::primary_unique<by_peer_row_id>,
           forge::db::object::ordered_unique<by_peer_id, forge::db::object::member<&peer_row::peer>>,
           forge::db::object::ordered_unique<
               by_peer_hydration,
               forge::db::object::composite_key<
                   forge::db::object::descending<forge::db::object::member<&peer_row::hydration_priority>>,
                   forge::db::object::descending<forge::db::object::member<&peer_row::discovery_expires_at_ns>>,
                   forge::db::object::member<&peer_row::peer>>>,
           forge::db::object::ordered_non_unique<by_peer_expiry,
                                                 forge::db::object::member<&peer_row::discovery_expires_at_ns>>>>;

   using provider_object = forge::db::object::object_index<
       provider_row,
       forge::db::object::indexed_by<
           forge::db::object::primary_unique<by_provider_row_id>,
           forge::db::object::ordered_unique<
               by_provider_key_peer, forge::db::object::composite_key<forge::db::object::member<&provider_row::key>,
                                                                      forge::db::object::member<&provider_row::peer>>>,
           forge::db::object::ordered_unique<
               by_provider_hydration,
               forge::db::object::composite_key<
                   forge::db::object::descending<forge::db::object::member<&provider_row::expires_at_ns>>,
                   forge::db::object::member<&provider_row::key>, forge::db::object::member<&provider_row::peer>>>,
           forge::db::object::ordered_non_unique<by_provider_expiry,
                                                 forge::db::object::member<&provider_row::expires_at_ns>>>>;

   using rendezvous_object = forge::db::object::object_index<
       rendezvous_row,
       forge::db::object::indexed_by<
           forge::db::object::primary_unique<by_rendezvous_row_id>,
           forge::db::object::ordered_unique<
               by_rendezvous_namespace_peer,
               forge::db::object::composite_key<forge::db::object::member<&rendezvous_row::namespace_name>,
                                                forge::db::object::member<&rendezvous_row::peer>>>,
           forge::db::object::ordered_unique<
               by_rendezvous_sequence,
               forge::db::object::composite_key<
                   forge::db::object::descending<forge::db::object::member<&rendezvous_row::sequence>>,
                   forge::db::object::member<&rendezvous_row::namespace_name>,
                   forge::db::object::member<&rendezvous_row::peer>>>,
           forge::db::object::ordered_non_unique<by_rendezvous_expiry,
                                                 forge::db::object::member<&rendezvous_row::expires_at_ns>>>>;

   static constexpr auto schema_state_id = schema_state::id_t{1};
};

} // namespace forge::plugins::p2p::node::detail

FORGE_DB_OBJECT(forge::plugins::p2p::node::detail::peer_state_schema::schema_state_object)
FORGE_DB_OBJECT(forge::plugins::p2p::node::detail::peer_state_schema::peer_object)
FORGE_DB_OBJECT(forge::plugins::p2p::node::detail::peer_state_schema::provider_object)
FORGE_DB_OBJECT(forge::plugins::p2p::node::detail::peer_state_schema::rendezvous_object)

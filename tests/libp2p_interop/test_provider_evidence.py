#!/usr/bin/env python3
import copy
import unittest

from provider_evidence import (
    HIDDEN_FIND_PEER_PROOF_SCHEMA,
    NETWORK_PROOF_SCHEMA,
    validate_hidden_find_peer_evidence,
    validate_provider_evidence,
)


def valid_result(implementation: str = "rust") -> dict:
    provider = "provider-peer"
    querier = "querier-peer"
    listener = "listener-peer"
    key = "1220provider-key"
    source = {
        "go": {
            "kind": "go_routing_query_events",
            "querier_peer": querier,
            "provider_key": key,
            "sending_query_events": 1,
        },
        "rust": {
            "kind": "rust_kad_query_stats",
            "query_id": "QueryId(7)",
            "matched_query_id": "QueryId(7)",
            "provider_key": key,
            "found_provider_peer": provider,
            "num_requests": 1,
            "num_successes": 1,
            "derivation": "matching_query_id_key_found_providers_query_stats",
        },
        "forge": {
            "kind": "forge_async_find_providers_result",
            "querier_peer": querier,
            "returned_provider_peer": provider,
            "provider_key": key,
            "opened_stream_protocol": "/ipfs/kad/1.0.0",
        },
    }[implementation]
    address = {
        "source": {
            "go": "find_providers_result",
            "rust": "kad_discovery_peer_id_dial",
            "forge": "get_providers_wire_reply",
        }[implementation],
        "provider_peer": provider,
        "address": "/ip4/127.0.0.1/udp/4001/quic-v1",
    }
    if implementation == "rust":
        address |= {
            "connection_id": "ConnectionId(9)",
            "immediate_after_found_providers": True,
            "authenticated_peer": provider,
        }
    proof = {
        "schema": NETWORK_PROOF_SCHEMA,
        "implementation": implementation,
        "provider_peer": provider,
        "querier_peer": querier,
        "listener_peer": listener,
        "provider_key": key,
        "key_binding": {
            "kind": "provider_identity_multihash",
            "provider_peer": provider,
            "provider_key": key,
            "derived_per_run": True,
        },
        "provider_registration": {
            "api": {"go": "Provide", "rust": "start_providing", "forge": "async_provide"}[implementation],
            "succeeded": True,
            "provider_peer": provider,
            "provider_key": key,
        },
        "api_lookup": {
            "api": {"go": "FindProvidersAsync", "rust": "get_providers", "forge": "async_find_providers"}[implementation],
            "succeeded": True,
            "querier_peer": querier,
            "returned_provider_peer": provider,
            "provider_key": key,
        },
        "address_proof": address,
        "protocol_proof": {
            "protocol": "/ipfs/kad/1.0.0",
            "derivation": {
                "go": "explicit_get_providers_wire_reply_selected_protocol",
                "rust": "sole_configured_ipfs_kad_1_0_0_successful_query_stats",
                "forge": "successful_async_open_protocol_stream_with_get_providers_wire_reply",
            }[implementation],
            "successful_query": True,
        },
        "source_query_proof": source,
        "wire_proof": None,
    }
    if implementation == "forge":
        proof["protocol_proof"]["opened_stream_protocol"] = "/ipfs/kad/1.0.0"
        proof["wire_proof"] = {
            "kind": "forge_get_providers_wire_reply",
            "explicit_wire_confirmation": True,
            "listener_peer": listener,
            "returned_provider_peer": provider,
            "address": address["address"],
            "opened_stream_protocol": "/ipfs/kad/1.0.0",
        }
    elif implementation == "go":
        proof["protocol_proof"]["opened_stream_protocol"] = "/ipfs/kad/1.0.0"
        proof["wire_proof"] = {
            "kind": "go_get_providers_wire_reply",
            "explicit_wire_confirmation": True,
            "listener_peer": listener,
            "returned_provider_peer": provider,
            "provider_key": key,
            "address": address["address"],
            "opened_stream_protocol": "/ipfs/kad/1.0.0",
        }
    return {
        "implementation": implementation,
        "role": "dialer",
        "scenario": "dht_provide_find_provider",
        "status": "ok",
        "network_proof": proof,
    }


def valid_hidden_find_peer_result() -> dict:
    seed = "seed-peer"
    target = "target-peer"
    key = "1220target-key"
    address = "/ip4/127.0.0.1/udp/4001/quic-v1"
    return {
        "implementation": "forge",
        "scenario": "dht_hidden_find_peer",
        "status": "ok",
        "preexisting_target": False,
        "found_peer": target,
        "negotiated_protocol": "/ipfs/kad/1.0.0",
        "find_peer_proof": {
            "schema": HIDDEN_FIND_PEER_PROOF_SCHEMA,
            "api": "async_find_peer",
            "api_succeeded": True,
            "querier_peer": "querier-peer",
            "seed_peer": seed,
            "target_peer": target,
            "target_key": key,
            "preexisting_target": False,
            "api_returned_target": target,
            "address": address,
            "protocol": "/ipfs/kad/1.0.0",
            "query_context": "fresh_hidden_target",
            "wire_confirmation": {
                "kind": "forge_find_node_wire_reply",
                "explicit_wire_confirmation": True,
                "seed_peer": seed,
                "target_peer": target,
                "target_key": key,
                "returned_target_peer": target,
                "address": address,
                "opened_stream_protocol": "/ipfs/kad/1.0.0",
            },
        },
    }


class ProviderEvidenceTest(unittest.TestCase):
    def assert_rejected(self, result: dict) -> None:
        self.assertTrue(validate_provider_evidence(result, "listener-peer"), result)

    def test_valid_closed_contract_for_each_implementation(self) -> None:
        for implementation in ("forge", "go", "rust"):
            with self.subTest(implementation=implementation):
                self.assertEqual(validate_provider_evidence(valid_result(implementation), "listener-peer"), [])

    def test_rejects_self_query(self) -> None:
        result = valid_result()
        result["network_proof"]["querier_peer"] = "provider-peer"
        self.assert_rejected(result)

    def test_rejects_provider_listener_alias(self) -> None:
        result = valid_result()
        result["network_proof"]["listener_peer"] = "provider-peer"
        self.assert_rejected(result)

    def test_rejects_zero_rust_successes(self) -> None:
        result = valid_result()
        result["network_proof"]["source_query_proof"]["num_successes"] = 0
        self.assert_rejected(result)

    def test_rejects_wrong_key_query_id_provider_protocol_source_and_address(self) -> None:
        mutations = (
            ("wrong key", lambda proof: proof["key_binding"].update({"provider_key": "wrong"})),
            ("wrong query id", lambda proof: proof["source_query_proof"].update({"matched_query_id": "QueryId(8)"})),
            ("wrong provider", lambda proof: proof["api_lookup"].update({"returned_provider_peer": "wrong"})),
            ("wrong protocol", lambda proof: proof["protocol_proof"].update({"protocol": "/ipfs/kad/1.1.0"})),
            ("wrong address source", lambda proof: proof["address_proof"].update({"source": "identify"})),
            ("wrong query source", lambda proof: proof["source_query_proof"].update({"derivation": "stream_count"})),
            ("missing address", lambda proof: proof["address_proof"].update({"address": ""})),
        )
        for label, mutate in mutations:
            with self.subTest(label=label):
                result = copy.deepcopy(valid_result())
                mutate(result["network_proof"])
                self.assert_rejected(result)

    def test_rejects_missing_proof_and_legacy_counters_cannot_bypass(self) -> None:
        result = valid_result("go")
        result["network_proof"].pop("source_query_proof")
        result["protocol_streams_opened_delta"] = 1
        result["query_requests_delta"] = 1
        self.assert_rejected(result)

    def test_hidden_find_peer_requires_fresh_api_and_decoded_find_node_reply(self) -> None:
        self.assertEqual(
            validate_hidden_find_peer_evidence(valid_hidden_find_peer_result(), "seed-peer", "target-peer"),
            [],
        )
        mutations = (
            ("self query", lambda proof: proof.update({"querier_peer": "target-peer"})),
            ("wrong seed", lambda proof: proof["wire_confirmation"].update({"seed_peer": "wrong-seed"})),
            ("wrong target key", lambda proof: proof["wire_confirmation"].update({"target_key": "wrong-key"})),
            ("wrong returned peer", lambda proof: proof.update({"api_returned_target": "wrong-target"})),
            ("wrong protocol", lambda proof: proof["wire_confirmation"].update({"opened_stream_protocol": "/ipfs/kad/1.1.0"})),
            ("stale context", lambda proof: proof.update({"query_context": "cached_target"})),
        )
        for label, mutate in mutations:
            with self.subTest(label=label):
                result = valid_hidden_find_peer_result()
                mutate(result["find_peer_proof"])
                self.assertTrue(validate_hidden_find_peer_evidence(result, "seed-peer", "target-peer"), result)

    def test_hidden_find_peer_rejects_legacy_counter_as_a_bypass(self) -> None:
        result = valid_hidden_find_peer_result()
        result["dht_queries_delta"] = 1
        self.assertTrue(validate_hidden_find_peer_evidence(result, "seed-peer", "target-peer"), result)


if __name__ == "__main__":
    unittest.main()

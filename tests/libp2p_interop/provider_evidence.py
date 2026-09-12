"""Closed evidence contract for independent Kademlia provider lookups.

The fixture implementations create the observations.  This module only checks
their cross-process bindings, so runner and Stage 6 share one fail-closed rule.
"""

from __future__ import annotations

from typing import Any, Optional


NETWORK_PROOF_SCHEMA = "forge.libp2p.provider-network-proof.v1"
HIDDEN_FIND_PEER_PROOF_SCHEMA = "forge.libp2p.hidden-find-peer-network-proof.v1"
KAD_PROTOCOL = "/ipfs/kad/1.0.0"
LEGACY_COUNTER_FIELDS = frozenset({
    "protocol_streams_opened_delta",
    "query_requests_delta",
    "dht_queries_delta",
})


def _nonempty_string(value: object) -> bool:
    return isinstance(value, str) and bool(value)


def _positive_integer(value: object) -> bool:
    return type(value) is int and value > 0


def _exact_mapping(value: object, fields: set[str], label: str, errors: list[str]) -> Optional[dict[str, Any]]:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return None
    if set(value) != fields:
        errors.append(f"{label} has an invalid closed shape")
        return None
    return value


def validate_provider_evidence(result: object, expected_listener_peer: Optional[str] = None) -> list[str]:
    """Return contract errors for one observed provider-lookup result.

    The accepted path deliberately has no generic stream/query counter.  Go
    binds routing query events to Q, Rust binds Kademlia QueryStats to its
    QueryId/key, and Forge binds a decoded GET_PROVIDERS wire reply to S.
    """
    errors: list[str] = []
    if not isinstance(result, dict):
        return ["provider result must be an object"]
    implementation = result.get("implementation")
    if implementation not in {"forge", "go", "rust"}:
        errors.append("provider result has an unknown implementation")
        return errors
    if result.get("status") != "ok" or result.get("scenario") != "dht_provide_find_provider":
        errors.append("provider result does not describe a successful provider lookup")
    legacy = LEGACY_COUNTER_FIELDS.intersection(result)
    if legacy:
        errors.append("provider result uses retired generic DHT counters")

    proof = _exact_mapping(
        result.get("network_proof"),
        {
            "schema",
            "implementation",
            "provider_peer",
            "querier_peer",
            "listener_peer",
            "provider_key",
            "key_binding",
            "provider_registration",
            "api_lookup",
            "address_proof",
            "protocol_proof",
            "source_query_proof",
            "wire_proof",
        },
        "network_proof",
        errors,
    )
    if proof is None:
        return errors
    if proof["schema"] != NETWORK_PROOF_SCHEMA or proof["implementation"] != implementation:
        errors.append("network_proof schema or implementation binding is invalid")

    provider_peer = proof["provider_peer"]
    querier_peer = proof["querier_peer"]
    listener_peer = proof["listener_peer"]
    provider_key = proof["provider_key"]
    if not all(_nonempty_string(value) for value in (provider_peer, querier_peer, listener_peer, provider_key)):
        errors.append("network_proof has an empty peer or provider key binding")
        return errors
    assert isinstance(provider_peer, str)
    assert isinstance(querier_peer, str)
    assert isinstance(listener_peer, str)
    assert isinstance(provider_key, str)
    if len({provider_peer, querier_peer, listener_peer}) != 3:
        errors.append("provider, querier and listener must be pairwise independent peers")
    if expected_listener_peer is not None and listener_peer != expected_listener_peer:
        errors.append("network_proof listener peer does not match the runner listener")

    key_binding = _exact_mapping(
        proof["key_binding"],
        {"kind", "provider_peer", "provider_key", "derived_per_run"},
        "network_proof.key_binding",
        errors,
    )
    if key_binding is not None and not (
        key_binding["kind"] == "provider_identity_multihash"
        and key_binding["provider_peer"] == provider_peer
        and key_binding["provider_key"] == provider_key
        and key_binding["derived_per_run"] is True
    ):
        errors.append("network_proof key binding is not derived from the provider identity")

    registration_api = {"forge": "async_provide", "go": "Provide", "rust": "start_providing"}[implementation]
    registration = _exact_mapping(
        proof["provider_registration"],
        {"api", "succeeded", "provider_peer", "provider_key"},
        "network_proof.provider_registration",
        errors,
    )
    if registration is not None and not (
        registration["api"] == registration_api
        and registration["succeeded"] is True
        and registration["provider_peer"] == provider_peer
        and registration["provider_key"] == provider_key
    ):
        errors.append("network_proof provider registration is not bound to P and its key")

    lookup_api = {"forge": "async_find_providers", "go": "FindProvidersAsync", "rust": "get_providers"}[implementation]
    lookup = _exact_mapping(
        proof["api_lookup"],
        {"api", "succeeded", "querier_peer", "returned_provider_peer", "provider_key"},
        "network_proof.api_lookup",
        errors,
    )
    if lookup is not None and not (
        lookup["api"] == lookup_api
        and lookup["succeeded"] is True
        and lookup["querier_peer"] == querier_peer
        and lookup["returned_provider_peer"] == provider_peer
        and lookup["provider_key"] == provider_key
    ):
        errors.append("network_proof successful API lookup is not bound to Q, P and the key")

    address_source = {
        "forge": "get_providers_wire_reply",
        "go": "find_providers_result",
        "rust": "kad_discovery_peer_id_dial",
    }[implementation]
    address_fields = {"source", "provider_peer", "address"}
    if implementation == "rust":
        address_fields |= {"connection_id", "immediate_after_found_providers", "authenticated_peer"}
    address = _exact_mapping(proof["address_proof"], address_fields, "network_proof.address_proof", errors)
    if address is not None and not (
        address["source"] == address_source
        and address["provider_peer"] == provider_peer
        and _nonempty_string(address["address"])
    ):
        errors.append("network_proof address proof is not bound to P")
    if implementation == "rust" and address is not None and not (
        _nonempty_string(address["connection_id"])
        and address["immediate_after_found_providers"] is True
        and address["authenticated_peer"] == provider_peer
    ):
        errors.append("Rust address proof lacks the immediate authenticated P peer-id dial binding")

    protocol_fields = {"protocol", "derivation", "successful_query"}
    if implementation in {"go", "forge"}:
        protocol_fields |= {"opened_stream_protocol"}
    protocol = _exact_mapping(proof["protocol_proof"], protocol_fields, "network_proof.protocol_proof", errors)
    protocol_derivation = {
        "go": "explicit_get_providers_wire_reply_selected_protocol",
        "rust": "sole_configured_ipfs_kad_1_0_0_successful_query_stats",
        "forge": "successful_async_open_protocol_stream_with_get_providers_wire_reply",
    }[implementation]
    if protocol is not None and not (
        protocol["protocol"] == KAD_PROTOCOL
        and protocol["derivation"] == protocol_derivation
        and protocol["successful_query"] is True
        and (implementation == "rust" or protocol["opened_stream_protocol"] == KAD_PROTOCOL)
    ):
        errors.append("network_proof protocol proof is not the implementation-specific Amino query proof")

    source = proof["source_query_proof"]
    if implementation == "go":
        source_fields = {"kind", "querier_peer", "provider_key", "sending_query_events"}
        source_valid = (
            isinstance(source, dict)
            and set(source) == source_fields
            and source.get("kind") == "go_routing_query_events"
            and source.get("querier_peer") == querier_peer
            and source.get("provider_key") == provider_key
            and _positive_integer(source.get("sending_query_events"))
        )
    elif implementation == "rust":
        source_fields = {"kind", "query_id", "matched_query_id", "provider_key", "found_provider_peer", "num_requests", "num_successes", "derivation"}
        source_valid = (
            isinstance(source, dict)
            and set(source) == source_fields
            and source.get("kind") == "rust_kad_query_stats"
            and _nonempty_string(source.get("query_id"))
            and source.get("matched_query_id") == source.get("query_id")
            and source.get("provider_key") == provider_key
            and source.get("found_provider_peer") == provider_peer
            and _positive_integer(source.get("num_requests"))
            and _positive_integer(source.get("num_successes"))
            and source.get("derivation") == "matching_query_id_key_found_providers_query_stats"
        )
    else:
        source_fields = {"kind", "querier_peer", "returned_provider_peer", "provider_key", "opened_stream_protocol"}
        source_valid = (
            isinstance(source, dict)
            and set(source) == source_fields
            and source.get("kind") == "forge_async_find_providers_result"
            and source.get("querier_peer") == querier_peer
            and source.get("returned_provider_peer") == provider_peer
            and source.get("provider_key") == provider_key
            and source.get("opened_stream_protocol") == KAD_PROTOCOL
        )
    if not source_valid:
        errors.append("network_proof source query proof is invalid")

    wire = proof["wire_proof"]
    if implementation == "forge":
        wire_valid = (
            isinstance(wire, dict)
            and set(wire) == {"kind", "explicit_wire_confirmation", "listener_peer", "returned_provider_peer", "address", "opened_stream_protocol"}
            and wire.get("kind") == "forge_get_providers_wire_reply"
            and wire.get("explicit_wire_confirmation") is True
            and wire.get("listener_peer") == listener_peer
            and wire.get("returned_provider_peer") == provider_peer
            and wire.get("address") == (address or {}).get("address")
            and wire.get("opened_stream_protocol") == KAD_PROTOCOL
        )
    elif implementation == "go":
        wire_valid = (
            isinstance(wire, dict)
            and set(wire) == {
                "kind",
                "explicit_wire_confirmation",
                "listener_peer",
                "returned_provider_peer",
                "provider_key",
                "address",
                "opened_stream_protocol",
            }
            and wire.get("kind") == "go_get_providers_wire_reply"
            and wire.get("explicit_wire_confirmation") is True
            and wire.get("listener_peer") == listener_peer
            and wire.get("returned_provider_peer") == provider_peer
            and wire.get("provider_key") == provider_key
            and wire.get("address") == (address or {}).get("address")
            and wire.get("opened_stream_protocol") == KAD_PROTOCOL
        )
    else:
        wire_valid = wire is None
    if not wire_valid:
        errors.append("network_proof wire proof is invalid")
    return errors


def validate_hidden_find_peer_evidence(
    result: object,
    expected_seed_peer: Optional[str] = None,
    expected_target_peer: Optional[str] = None,
) -> list[str]:
    """Return fail-closed errors for Forge's independent hidden-peer lookup."""
    errors: list[str] = []
    if not isinstance(result, dict):
        return ["hidden FindPeer result must be an object"]
    if (
        result.get("implementation") != "forge"
        or result.get("status") != "ok"
        or result.get("scenario") != "dht_hidden_find_peer"
    ):
        errors.append("hidden FindPeer result does not describe a successful Forge lookup")
    if "dht_queries_delta" in result:
        errors.append("hidden FindPeer result uses a retired incoming DHT counter")

    proof = _exact_mapping(
        result.get("find_peer_proof"),
        {
            "schema",
            "api",
            "api_succeeded",
            "querier_peer",
            "seed_peer",
            "target_peer",
            "target_key",
            "preexisting_target",
            "api_returned_target",
            "address",
            "protocol",
            "query_context",
            "wire_confirmation",
        },
        "find_peer_proof",
        errors,
    )
    if proof is None:
        return errors
    scalar_fields = ("querier_peer", "seed_peer", "target_peer", "target_key", "api_returned_target", "address")
    if proof["schema"] != HIDDEN_FIND_PEER_PROOF_SCHEMA or not all(
        _nonempty_string(proof[field]) for field in scalar_fields
    ):
        errors.append("hidden FindPeer proof schema or identity binding is invalid")
        return errors
    target_peer = proof["target_peer"]
    seed_peer = proof["seed_peer"]
    if not isinstance(target_peer, str) or not isinstance(seed_peer, str):
        errors.append("hidden FindPeer proof peer bindings are invalid")
        return errors
    if expected_seed_peer is not None and seed_peer != expected_seed_peer:
        errors.append("hidden FindPeer proof seed does not match the routing listener")
    if expected_target_peer is not None and target_peer != expected_target_peer:
        errors.append("hidden FindPeer proof target does not match the hidden listener")
    if not (
        proof["api"] == "async_find_peer"
        and proof["api_succeeded"] is True
        and proof["preexisting_target"] is False
        and proof["api_returned_target"] == target_peer
        and proof["protocol"] == KAD_PROTOCOL
        and proof["query_context"] == "fresh_hidden_target"
        and len({proof["querier_peer"], seed_peer, target_peer}) == 3
    ):
        errors.append("hidden FindPeer API proof is not bound to a fresh Q, S and target")

    wire = _exact_mapping(
        proof["wire_confirmation"],
        {
            "kind",
            "explicit_wire_confirmation",
            "seed_peer",
            "target_peer",
            "target_key",
            "returned_target_peer",
            "address",
            "opened_stream_protocol",
        },
        "find_peer_proof.wire_confirmation",
        errors,
    )
    if wire is not None and not (
        wire["kind"] == "forge_find_node_wire_reply"
        and wire["explicit_wire_confirmation"] is True
        and wire["seed_peer"] == seed_peer
        and wire["target_peer"] == target_peer
        and wire["target_key"] == proof["target_key"]
        and wire["returned_target_peer"] == target_peer
        and wire["address"] == proof["address"]
        and wire["opened_stream_protocol"] == KAD_PROTOCOL
    ):
        errors.append("hidden FindPeer wire confirmation is not bound to S, target, key and address")
    return errors

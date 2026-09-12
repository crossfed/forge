"""Correlate authoritative DNS observations with the authenticated donor call."""


DNSADDR_SCENARIOS = {
    "tcp_stage6/dnsaddr": "echo",
    "private_tcp_yamux_pnet/dnsaddr_private_tcp_yamux_pnet": "pnet",
}


def validate_dnsaddr(result, record):
    evidence = record.get("dns_evidence")
    if not isinstance(evidence, dict):
        return ["DNSADDR evidence lacks authoritative DNS observations"]
    root = evidence.get("root")
    peer = record.get("peer_id")
    if not isinstance(root, str) or not root.startswith("/dnsaddr/") or not isinstance(peer, str):
        return ["DNSADDR evidence lacks a typed root and expected peer"]
    parts = root.split("/")
    if len(parts) != 5 or parts[3:] != ["p2p", peer]:
        return ["DNSADDR root is not bound to the expected peer"]
    domain = parts[2]
    records = evidence.get("records")
    names = [f"_dnsaddr.{domain}", f"_dnsaddr.target.{domain}"]
    if not isinstance(records, dict) or set(records) != set(names):
        return ["DNSADDR evidence does not describe its exact two-hop TXT chain"]
    listener = record.get("listener_process", {})
    addresses = listener.get("listen_addrs", []) if isinstance(listener, dict) else []
    if (records[names[0]] != f"dnsaddr=/dnsaddr/target.{domain}/p2p/{peer}"
            or records[names[1]] not in [f"dnsaddr={address}" for address in addresses]):
        return ["DNSADDR answers do not resolve to the recorded listener"]
    queries = evidence.get("queries")
    if not isinstance(queries, list) or not 2 <= len(queries) <= 1024:
        return ["DNSADDR evidence lacks bounded actual queries"]
    answered = {entry.get("name") for entry in queries if isinstance(entry, dict)
                and entry.get("type") == 16 and entry.get("answered") is True}
    if not set(names).issubset(answered):
        return ["DNSADDR did not query both TXT hops"]
    if (result.get("dns_input_address") != root or result.get("dns_resolver_configured") is not True
            or result.get("expected_peer_id") != peer or result.get("authenticated_remote_peer_id") != peer):
        return ["DNSADDR input, resolver and authenticated peer are not correlated"]
    return []

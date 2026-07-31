#!/usr/bin/env python3

import json
import pathlib
import re
import sys


root = pathlib.Path(sys.argv[1]).resolve()
manifest_path = root / "tests/chain_api/spring_api_manifest.json"
manifest = json.loads(manifest_path.read_text())

expected_commit = "e6a99f68b67abc4d89fe716755b2e1394a4991f7"
if manifest["schema"] != 1 or manifest["donor"]["commit"] != expected_commit:
    raise SystemExit("Spring API manifest baseline changed")

expected_sources = {
    "chain_source": (
        "plugins/chain_api_plugin/chain_api_plugin.cpp",
        "913f705ccea61c6fc9f5926c00dee2e14c03e4fdc7fa543f0db89544da0d8ab5",
    ),
    "producer_source": (
        "plugins/producer_api_plugin/producer_api_plugin.cpp",
        "1e159c03859c5e7240dac74745cce646560215c1a815e41ee2d1abcdd9086eda",
    ),
}
for key, (path, digest) in expected_sources.items():
    if manifest["donor"].get(key) != path or manifest["donor"].get(f"{key}_sha256") != digest:
        raise SystemExit(f"Spring API donor source changed: {key}")

endpoints = manifest["endpoints"]
group_counts = {
    group: sum(endpoint["group"] == group for endpoint in endpoints)
    for group in ("chain", "producer")
}
if group_counts != {"chain": 33, "producer": 21}:
    raise SystemExit(f"Spring API manifest has wrong endpoint counts: {group_counts}")

donor_keys = [(endpoint["group"], endpoint["donor"]) for endpoint in endpoints]
if len(donor_keys) != len(set(donor_keys)):
    raise SystemExit("Spring API manifest contains duplicate donor endpoints")

allowed_mappings = {"direct", "projection", "client_projection", "local_utility"}
api_sources = {}
for api in ("info", "block", "state", "transaction", "admin"):
    path = root / f"libraries/chain/api/include/forge/chain/api/{api}.cppm"
    api_sources[api] = path.read_text()

for endpoint in endpoints:
    mapping = endpoint["mapping"]
    if mapping not in allowed_mappings:
        raise SystemExit(f"unsupported mapping for {endpoint['donor']}: {mapping}")
    api = endpoint["forge_api"]
    method = endpoint["forge_method"]
    if api == "protocol":
        if (method, mapping) != ("transaction.id", "local_utility"):
            raise SystemExit(f"invalid local utility mapping for {endpoint['donor']}")
        continue
    if api not in api_sources:
        raise SystemExit(f"unknown Forge API for {endpoint['donor']}: {api}")
    if re.search(rf"\b{re.escape(method)}\s*\(", api_sources[api]) is None:
        raise SystemExit(f"missing Forge API method for {endpoint['donor']}: {api}.{method}")

print(f"Spring API donor manifest verified: {group_counts['chain']} chain + {group_counts['producer']} producer")

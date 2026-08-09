#!/usr/bin/env python3
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path


REQUIRED_FIELDS = {
    "id",
    "state",
    "owner",
    "normal_activation",
    "configuration",
    "resource_ownership",
    "persistence",
    "maintenance",
    "diagnostics",
    "intended_disposition",
    "builtin_protocols",
    "capabilities",
    "evidence",
}

EVIDENCE_LAYERS = {
    "codec",
    "state_machine",
    "raw_node",
    "official_plugin",
    "restart_scale",
    "adversarial",
    "donor_interop",
}

REQUIRED_FEATURE_IDS = {
    "transport.direct_quic",
    "transport.tcp_yamux",
    "protocol.multistream_select",
    "identity.secure_transport_authentication",
    "protocol.echo",
    "protocol.ping",
    "protocol.identify",
    "protocol.peer_exchange",
    "protocol.autonat",
    "protocol.relay",
    "protocol.dcutr",
    "state.hole_punch_attempt",
    "protocol.kademlia",
    "protocol.rendezvous",
    "protocol.gossipsub",
    "state.peer_store",
    "state.kademlia_routing_table",
    "lifecycle.bootstrap",
    "lifecycle.discovery",
    "resource.sessions",
    "resource.streams",
    "resource.dials",
    "resource.queued_bytes",
    "topology.connection_manager",
    "plugin.node",
    "plugin.resolver",
    "plugin.pubsub",
    "plugin.diagnostics",
}

REQUIRED_OWNERS = {
    "net.p2p.node": {
        "kind": "library",
        "path": "libraries/net/p2p",
        "target": "forge_net_p2p",
        "component": "net_p2p",
        "module_prefix": "forge.net.p2p",
        "module_root": "net/p2p",
    },
    "plugin.p2p.node": {
        "kind": "plugin",
        "path": "plugins/p2p/node",
        "target": "forge_plugins_p2p_node",
        "component": "plugins_p2p_node",
        "module_prefix": "forge.plugins.p2p.node",
        "module_root": "plugins/p2p/node",
    },
    "plugin.p2p.resolver": {
        "kind": "plugin",
        "path": "plugins/p2p/resolver",
        "target": "forge_plugins_p2p_resolver",
        "component": "plugins_p2p_resolver",
        "module_prefix": "forge.plugins.p2p.resolver",
        "module_root": "plugins/p2p/resolver",
    },
    "plugin.p2p.pubsub": {
        "kind": "plugin",
        "path": "plugins/p2p/pubsub",
        "target": "forge_plugins_p2p_pubsub",
        "component": "plugins_p2p_pubsub",
        "module_prefix": "forge.plugins.p2p.pubsub",
        "module_root": "plugins/p2p/pubsub",
    },
    "plugin.p2p.diagnostics": {
        "kind": "plugin",
        "path": "plugins/p2p/diagnostics",
        "target": "forge_plugins_p2p_diagnostics",
        "component": "plugins_p2p_diagnostics",
        "module_prefix": "forge.plugins.p2p.diagnostics",
        "module_root": "plugins/p2p/diagnostics",
    },
    "application": {"kind": "external"},
    "none": {"kind": "none"},
}


def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def extract_namespace_names(source: str, namespace: str, declaration: str) -> set[str]:
    match = re.search(rf"namespace {namespace}\s*\{{(?P<body>.*?)\n\}}", source, re.DOTALL)
    if match is None:
        return set()
    return set(re.findall(declaration, match.group("body")))


def has_registered_live_interop(case: object) -> bool:
    if not isinstance(case, dict):
        return False
    scenarios = case.get("forge_live_scenario", [])
    tests = case.get("forge_tests", [])
    if case.get("mapping_state") != "mapped":
        return False
    if not isinstance(scenarios, list) or not scenarios or any(
        not isinstance(selector, dict)
        or set(selector) != {"profile", "scenario"}
        or not isinstance(selector.get("profile"), str)
        or not isinstance(selector.get("scenario"), str)
        for selector in scenarios
    ):
        return False
    if not isinstance(tests, list):
        return False
    return any(
        isinstance(reference, str)
        and reference.strip().split()[0] == "test_forge_libp2p_interop"
        for reference in tests
        if reference.strip()
    )


def public_surface_snapshot(
    root: Path, owner: dict[str, str]
) -> tuple[list[str], list[str], str, list[str]]:
    public_root = root / owner["path"] / "include/forge" / owner["module_root"]
    sources = sorted(public_root.glob("*.cppm"))
    headers = sorted([*public_root.glob("*.hpp"), *public_root.glob("*.h")])
    nested = sorted(
        source.relative_to(root).as_posix()
        for source in public_root.rglob("*")
        if source.is_file()
        and source.suffix in {".cppm", ".hpp", ".h"}
        and source.parent != public_root
    )
    digest = hashlib.sha256()
    modules: list[str] = []
    for source in [*sources, *headers]:
        modules.extend(
            re.findall(
                r"(?m)^(?:export\s+)?module\s+([A-Za-z0-9_.]+)\s*;",
                source.read_text(),
            )
        )
        digest.update(source.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(source.read_bytes())
        digest.update(b"\0")
    header_paths = [header.relative_to(root).as_posix() for header in headers]
    return modules, header_paths, digest.hexdigest(), nested


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: check_p2p_feature_inventory.py SOURCE_ROOT INVENTORY DONOR_CASES",
            file=sys.stderr,
        )
        return 2

    root = Path(sys.argv[1]).resolve()
    inventory_path = Path(sys.argv[2]).resolve()
    donor_path = Path(sys.argv[3]).resolve()
    errors: list[str] = []
    try:
        inventory = json.loads(inventory_path.read_text(), object_pairs_hook=reject_duplicate_keys)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: inventory: {error}", file=sys.stderr)
        return 1
    try:
        donor = json.loads(donor_path.read_text(), object_pairs_hook=reject_duplicate_keys)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: donor matrix: {error}", file=sys.stderr)
        return 1
    if not isinstance(inventory, dict):
        print("ERROR: inventory: top-level value must be an object", file=sys.stderr)
        return 1
    if not isinstance(donor, dict):
        print("ERROR: donor matrix: top-level value must be an object", file=sys.stderr)
        return 1

    if inventory.get("schema_version") != 1:
        errors.append("inventory: unsupported schema_version")
    if inventory.get("claim_scope") != "source_structure_and_declared_evidence_only":
        errors.append("inventory: claim_scope must not imply executed runtime evidence")

    expected_states = {"live", "manual-only", "partial", "stub", "orphan", "unverified"}
    allowed_states_value = inventory.get("allowed_states", [])
    if not isinstance(allowed_states_value, list) or any(
        not isinstance(value, str) for value in allowed_states_value
    ):
        errors.append("inventory: allowed_states must be an array of strings")
        allowed_states: set[str] = set()
    else:
        allowed_states = set(allowed_states_value)
    if allowed_states != expected_states:
        errors.append("inventory: allowed_states must match the accepted hardening vocabulary")

    owners = inventory.get("owners", {})
    if not isinstance(owners, dict):
        errors.append("inventory: owners must be an object")
        owners = {}
    elif owners != REQUIRED_OWNERS:
        errors.append("inventory: owners must match the canonical P2P target/component/module mapping")
    root_cmake = (root / "CMakeLists.txt").read_text()
    package_config = (root / "cmake/ForgeConfig.cmake.in").read_text()
    for owner_id, owner in REQUIRED_OWNERS.items():
        kind = owner.get("kind", "")
        if kind in {"library", "plugin"}:
            path = owner.get("path", "")
            relative_path = Path(path)
            if (
                not path
                or relative_path.is_absolute()
                or ".." in relative_path.parts
                or not (root / relative_path).is_dir()
            ):
                errors.append(f"owner {owner_id}: path must reference a repository directory")
            for field in ("target", "component", "module_prefix", "module_root"):
                if not owner.get(field, ""):
                    errors.append(f"owner {owner_id}: missing {field}")
            cmake_path = root / relative_path / "CMakeLists.txt"
            cmake_source = cmake_path.read_text() if cmake_path.is_file() else ""
            target = owner.get("target", "")
            target_declaration = rf"add_library\s*\(\s*{re.escape(target)}(?:\s|\))"
            if not cmake_path.is_file() or not re.search(target_declaration, cmake_source):
                errors.append(f"owner {owner_id}: target is not declared by its CMakeLists.txt")
            module_root = owner.get("module_root", "")
            module_registration = (
                rf"forge_target_modules_at\s*\(\s*{re.escape(target)}\s+"
                rf"{re.escape(module_root)}\s*\)"
            )
            if target and module_root and not re.search(module_registration, cmake_source):
                errors.append(
                    f"owner {owner_id}: public modules are not registered to the canonical target/root"
                )
            component = owner.get("component", "")
            if not re.search(rf"(?m)^\s*{re.escape(component)}\s*$", root_cmake):
                errors.append(f"owner {owner_id}: component is not listed in FORGE_BUILT_COMPONENTS")
            if f'"{component}"' not in package_config:
                errors.append(f"owner {owner_id}: component is not handled by ForgeConfig.cmake.in")
            module_prefix = owner.get("module_prefix", "")
            module_sources = list((root / relative_path).glob("include/**/*.cppm"))
            declared_modules = {
                module
                for source in module_sources
                for module in re.findall(
                    r"(?m)^(?:export\s+)?module\s+([A-Za-z0-9_.]+)\s*;",
                    source.read_text(),
                )
            }
            if not declared_modules:
                errors.append(f"owner {owner_id}: no public modules were discovered")
            elif module_prefix and any(
                module != module_prefix and not module.startswith(f"{module_prefix}.")
                for module in declared_modules
            ):
                errors.append(f"owner {owner_id}: public module outside canonical module_prefix")
        elif kind not in {"external", "none"}:
            errors.append(f"owner {owner_id}: unknown kind {kind!r}")

    surface_snapshots = inventory.get("public_surface_snapshots", {})
    repository_owners = {
        owner_id for owner_id, owner in REQUIRED_OWNERS.items() if owner["kind"] in {"library", "plugin"}
    }
    if not isinstance(surface_snapshots, dict):
        errors.append("inventory: public_surface_snapshots must be an object")
        surface_snapshots = {}
    elif set(surface_snapshots) != repository_owners:
        errors.append("inventory: public_surface_snapshots must cover every repository P2P owner exactly once")

    donor_cases = donor.get("cases", [])
    if not isinstance(donor_cases, list):
        errors.append("donor matrix: cases must be an array")
        donor_cases = []
    donor_by_id = {
        case.get("id", ""): case
        for case in donor_cases
        if isinstance(case, dict) and isinstance(case.get("id", ""), str)
    }
    donor_ids = set(donor_by_id)
    if donor.get("status_scope") != "donor_case_coverage_only":
        errors.append("donor matrix: status_scope must prevent production interpretation")
    if donor.get("execution_scope") != "registered_optional_tests_not_current_results":
        errors.append("donor matrix: execution_scope must not imply current interop results")
    if donor.get("production_inventory") != inventory_path.name:
        errors.append("donor matrix: production_inventory must reference the feature inventory")

    feature_ids: set[str] = set()
    builtin_coverage: Counter[str] = Counter()
    capability_coverage: Counter[str] = Counter()
    negotiated_protocol_coverage: Counter[str] = Counter()
    public_component_coverage: Counter[str] = Counter()
    test_manifest = (root / "tests/CMakeLists.txt").read_text()
    registered_tests = set(
        re.findall(
            r"add_(?:executable|custom_target)\s*\(\s*([A-Za-z0-9_]+)",
            test_manifest,
            re.DOTALL,
        )
    )
    registered_tests.update(
        re.findall(r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)", test_manifest, re.DOTALL)
    )

    features = inventory.get("features", [])
    if not isinstance(features, list):
        errors.append("inventory: features must be an array")
        features = []
    for feature in features:
        if not isinstance(feature, dict):
            errors.append("inventory: every feature must be an object")
            continue
        feature_id = feature.get("id", "")
        if not isinstance(feature_id, str) or not feature_id:
            errors.append("feature without id")
            continue
        if feature_id in feature_ids:
            errors.append(f"{feature_id}: duplicate feature id")
        feature_ids.add(feature_id)

        missing = REQUIRED_FIELDS - feature.keys()
        if missing:
            errors.append(f"{feature_id}: missing fields {sorted(missing)}")
            continue

        string_values: dict[str, str] = {}
        for field in (
            "owner",
            "normal_activation",
            "configuration",
            "resource_ownership",
            "persistence",
            "maintenance",
            "diagnostics",
            "intended_disposition",
        ):
            value = feature[field]
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{feature_id}: {field} must be a non-empty string")
                string_values[field] = ""
            else:
                string_values[field] = value.strip()

        state_value = feature["state"]
        state = state_value if isinstance(state_value, str) else ""
        if not state:
            errors.append(f"{feature_id}: state must be a non-empty string")
        owner = string_values["owner"]
        if state not in allowed_states:
            errors.append(f"{feature_id}: unknown state {state!r}")
        if owner not in REQUIRED_OWNERS:
            errors.append(f"{feature_id}: unknown owner {owner!r}")

        if state == "orphan" and owner != "none":
            errors.append(f"{feature_id}: orphan feature must have owner 'none'")
        if state != "orphan" and owner == "none":
            errors.append(f"{feature_id}: owner 'none' requires orphan state")
        if state == "manual-only" and "explicit" not in string_values["normal_activation"]:
            errors.append(f"{feature_id}: manual-only feature must identify explicit activation")
        if state == "stub" and not any(
            action in string_values["intended_disposition"] for action in ("replace", "reject", "remove")
        ):
            errors.append(f"{feature_id}: stub must be replaced, rejected or removed")

        list_values: dict[str, list[str]] = {}
        for field in ("builtin_protocols", "capabilities", "negotiated_protocol_ids", "public_components"):
            value = feature.get(field, [])
            if not isinstance(value, list) or any(
                not isinstance(item, str) or not item.strip() for item in value
            ):
                errors.append(f"{feature_id}: {field} must be an array of non-empty strings")
                list_values[field] = []
            else:
                list_values[field] = value

        evidence = feature["evidence"]
        if not isinstance(evidence, dict):
            errors.append(f"{feature_id}: evidence must be an object")
            continue

        evidence_values: dict[str, list[str]] = {}
        for field in ("layers", "source_paths", "tests", "donor_sources", "donor_cases"):
            value = evidence.get(field, [])
            if not isinstance(value, list) or any(
                not isinstance(item, str) or not item.strip() for item in value
            ):
                errors.append(f"{feature_id}: evidence.{field} must be an array of non-empty strings")
                evidence_values[field] = []
            else:
                evidence_values[field] = value

        layers = set(evidence_values["layers"])
        unknown_layers = layers - EVIDENCE_LAYERS
        if unknown_layers:
            errors.append(f"{feature_id}: unknown evidence layers {sorted(unknown_layers)}")
        if state == "live":
            required_live = EVIDENCE_LAYERS
            if not required_live <= layers:
                errors.append(
                    f"{feature_id}: live claim lacks evidence layers {sorted(required_live - layers)}"
                )
            if string_values["diagnostics"].lower() in {"none", "unavailable"}:
                errors.append(f"{feature_id}: live claim lacks diagnostics")

        source_paths = evidence_values["source_paths"]
        tests = evidence_values["tests"]
        donor_sources = evidence_values["donor_sources"]
        case_ids = evidence_values["donor_cases"]
        if not source_paths:
            errors.append(f"{feature_id}: evidence must list source_paths")
        if not tests:
            errors.append(f"{feature_id}: evidence must list tests")
        if not donor_sources:
            errors.append(f"{feature_id}: evidence must list donor_sources")
        if state == "live" and not case_ids:
            errors.append(f"{feature_id}: live claim must reference donor cases")
        if state == "live" and not any(path.startswith("plugins/") for path in source_paths):
            errors.append(f"{feature_id}: live claim must reference its official-plugin path")
        if state == "live" and not any(test.startswith("test_forge_plugins") for test in tests):
            errors.append(f"{feature_id}: live claim must reference an official-plugin test target")
        if state == "live" and not any(
            has_registered_live_interop(donor_by_id.get(case_id)) for case_id in case_ids
        ):
            errors.append(f"{feature_id}: live claim must reference a donor case with registered live interop")

        for relative in source_paths + donor_sources:
            if Path(relative).is_absolute() or ".." in Path(relative).parts:
                errors.append(f"{feature_id}: evidence path must be repository-relative: {relative}")
            elif not (root / relative).is_file():
                errors.append(f"{feature_id}: evidence path does not exist: {relative}")

        for case_id in case_ids:
            if case_id not in donor_ids:
                errors.append(f"{feature_id}: unknown donor case {case_id!r}")
        for test in tests:
            if test not in registered_tests:
                errors.append(f"{feature_id}: unknown test target {test!r}")

        builtin_coverage.update(list_values["builtin_protocols"])
        capability_coverage.update(list_values["capabilities"])
        negotiated_protocol_coverage.update(list_values["negotiated_protocol_ids"])
        public_component_coverage.update(list_values["public_components"])

    missing_features = REQUIRED_FEATURE_IDS - feature_ids
    unknown_features = feature_ids - REQUIRED_FEATURE_IDS
    if missing_features:
        errors.append(f"inventory: missing required features {sorted(missing_features)}")
    if unknown_features:
        errors.append(f"inventory: unknown features require checker ownership {sorted(unknown_features)}")

    expected_surface_features: dict[str, list[str]] = {owner: [] for owner in repository_owners}
    for feature in features:
        if not isinstance(feature, dict) or not isinstance(feature.get("id"), str):
            continue
        owner = feature.get("owner")
        surface_owner = owner if owner in repository_owners else "net.p2p.node"
        expected_surface_features[surface_owner].append(feature["id"])
    for owner_id in sorted(repository_owners):
        snapshot = surface_snapshots.get(owner_id, {})
        if not isinstance(snapshot, dict):
            errors.append(f"public surface {owner_id}: snapshot must be an object")
            continue
        owner = REQUIRED_OWNERS[owner_id]
        modules, headers, digest, nested = public_surface_snapshot(root, owner)
        if nested:
            errors.append(f"public surface {owner_id}: nested public source files are forbidden {nested}")
        if snapshot.get("owner") != owner_id:
            errors.append(f"public surface {owner_id}: owner must be exact")
        if snapshot.get("module_count") != len(modules):
            errors.append(f"public surface {owner_id}: public module count changed")
        if snapshot.get("modules") != modules:
            errors.append(f"public surface {owner_id}: public module inventory changed")
        if snapshot.get("headers") != headers:
            errors.append(f"public surface {owner_id}: public macro-header inventory changed")
        if snapshot.get("sha256") != digest:
            errors.append(
                f"public surface {owner_id}: declarations changed; update feature classification deliberately"
            )
        snapshot_features = snapshot.get("feature_ids", [])
        if not isinstance(snapshot_features, list) or any(
            not isinstance(feature_id, str) or not feature_id.strip()
            for feature_id in snapshot_features
        ):
            errors.append(f"public surface {owner_id}: feature_ids must be non-empty strings")
        elif sorted(snapshot_features) != sorted(expected_surface_features[owner_id]):
            errors.append(f"public surface {owner_id}: feature_ids do not match owned public surface")
        module_features = snapshot.get("module_features", {})
        if not isinstance(module_features, dict) or set(module_features) != set(modules):
            errors.append(f"public surface {owner_id}: every module needs exact feature classification")
            module_features = {}
        classified_features: set[str] = set()
        for module, classified in module_features.items():
            if not isinstance(classified, list) or not classified or any(
                not isinstance(feature_id, str) or feature_id not in feature_ids
                for feature_id in classified
            ):
                errors.append(f"public surface {owner_id}: invalid feature classification for {module}")
                continue
            classified_features.update(classified)
        header_features = snapshot.get("header_features", {})
        if not isinstance(header_features, dict) or set(header_features) != set(headers):
            errors.append(f"public surface {owner_id}: every macro header needs exact feature classification")
            header_features = {}
        for header, classified in header_features.items():
            if not isinstance(classified, list) or not classified or any(
                not isinstance(feature_id, str) or feature_id not in feature_ids
                for feature_id in classified
            ):
                errors.append(f"public surface {owner_id}: invalid feature classification for {header}")
                continue
            classified_features.update(classified)
        if classified_features != set(expected_surface_features[owner_id]):
            errors.append(f"public surface {owner_id}: module/header classifications miss owned features")

    protocol_path = root / "libraries/net/p2p/include/forge/net/p2p/protocol.cppm"
    protocol_source = protocol_path.read_text()
    declared_builtins = extract_namespace_names(
        protocol_source,
        "builtins",
        r"inline const protocol_id\s+([a-zA-Z0-9_]+)",
    )
    declared_capabilities = extract_namespace_names(
        protocol_source,
        "capabilities",
        r"inline constexpr std::uint64_t\s+([a-zA-Z0-9_]+)",
    )
    builtins_match = re.search(r"namespace builtins\s*\{(?P<body>.*?)\n\}", protocol_source, re.DOTALL)
    builtin_values = (
        set(re.findall(r'\.value\s*=\s*"([^"]+)"', builtins_match.group("body"))) if builtins_match else set()
    )
    p2p_sources = list((root / "libraries/net/p2p").glob("*.cpp"))
    p2p_sources.extend((root / "libraries/net/p2p/include").glob("**/*.cppm"))
    p2p_sources.extend((root / "plugins/p2p").glob("**/*.cpp"))
    p2p_sources.extend((root / "plugins/p2p").glob("**/*.cppm"))
    protocol_literals = {
        value
        for source in p2p_sources
        for value in re.findall(
            r'protocol_id(?:\s+[A-Za-z0-9_]+)?\s*\{\s*\.value\s*=\s*"([^"]+)"',
            source.read_text(),
        )
        if value.startswith("/")
    }
    declared_negotiated_protocols = protocol_literals - builtin_values
    public_components = {
        component
        for source in (root / "libraries/net/p2p/include").glob("**/*.cppm")
        for component in re.findall(
            r"(?m)^class\s+([A-Za-z_][A-Za-z0-9_]*::[A-Za-z_][A-Za-z0-9_]*)\s*[{:]",
            source.read_text(),
        )
    }

    for kind, declared, coverage in (
        ("built-in protocol", declared_builtins, builtin_coverage),
        ("capability", declared_capabilities, capability_coverage),
        ("negotiated protocol", declared_negotiated_protocols, negotiated_protocol_coverage),
        ("public nested component", public_components, public_component_coverage),
    ):
        unknown = set(coverage) - declared
        missing = declared - set(coverage)
        duplicates = sorted(name for name, count in coverage.items() if count != 1)
        if unknown:
            errors.append(f"inventory: unknown {kind}s {sorted(unknown)}")
        if missing:
            errors.append(f"inventory: missing {kind}s {sorted(missing)}")
        if duplicates:
            errors.append(f"inventory: {kind}s must be owned exactly once {duplicates}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(
        "P2P source inventory valid: "
        f"{len(feature_ids)} features, "
        f"{len(declared_builtins)} built-in protocols, "
        f"{len(declared_capabilities)} capabilities, "
        f"{len(declared_negotiated_protocols)} negotiated protocols, "
        f"{len(public_components)} public nested components"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

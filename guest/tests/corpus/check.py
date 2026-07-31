#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path


DONORS = {
    "spring": {
        "repository": "https://github.com/AntelopeIO/spring.git",
        "commit": "e6a99f68b67abc4d89fe716755b2e1394a4991f7",
    },
    "cdt": {
        "repository": "https://github.com/AntelopeIO/cdt.git",
        "commit": "69599db279b7b93d0688502720c15c6962a1401b",
    },
    "eosio": {
        "repository": "https://github.com/EOSIO/eos.git",
        "commit": "11d35f0f934402321853119d36caeb7022813743",
        "description": "v2.1.0-7-g11d35f0f9",
    },
}

CONTRACTS = {
    "spring_eosio_boot": "spring/contracts/eosio.boot/eosio.boot.abi",
    "spring_eosio_token": "spring/contracts/eosio.token/eosio.token.abi",
    "spring_eosio_msig": "spring/contracts/eosio.msig/eosio.msig.abi",
    "spring_eosio_wrap": "spring/contracts/eosio.wrap/eosio.wrap.abi",
    "spring_eosio_system": "spring/contracts/eosio.system/eosio.system.abi",
    "spring_test_api": None,
    "spring_test_api_db": "spring/test-contracts/test_api_db/test_api_db.abi",
    "spring_test_api_multi_index": "spring/test-contracts/test_api_multi_index/test_api_multi_index.abi",
    "eosio_bios": "eosio/contracts/eosio.bios/bin/eosio.bios.abi",
    "eosio_boot": "eosio/contracts/eosio.boot/bin/eosio.boot.abi",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_for(relative: Path) -> tuple[str, str]:
    parts = relative.parts
    if parts[:4] == ("spring", "test-contracts", "capi", "eosio"):
        return "cdt", "libraries/eosiolib/capi/eosio/types.h"
    if parts[0] == "spring" and parts[1] == "contracts":
        return "spring", str(Path("unittests/contracts", *parts[2:]))
    if parts[0] == "spring" and parts[1] == "test-contracts":
        return "spring", str(Path("unittests/test-contracts", *parts[2:]))
    if parts[0] == "eosio" and parts[1] == "contracts":
        return "eosio", str(Path("contracts/contracts", *parts[2:]))
    raise ValueError(f"unmapped corpus path: {relative}")


def corpus_files(root: Path) -> list[Path]:
    return sorted(
        path
        for donor in (root / "spring", root / "eosio")
        for path in donor.rglob("*")
        if path.is_file() and "__pycache__" not in path.parts
    )


def write_provenance(root: Path) -> None:
    files = []
    for path in corpus_files(root):
        relative = path.relative_to(root)
        donor, source = source_for(relative)
        files.append(
            {
                "path": str(relative),
                "donor": donor,
                "source": source,
                "sha256": digest(path),
            }
        )
    output = {"schema": 1, "donors": DONORS, "files": files}
    (root / "provenance.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")


def check_integrity(root: Path) -> None:
    provenance = json.loads((root / "provenance.json").read_text(encoding="utf-8"))
    if provenance["donors"] != DONORS:
        raise SystemExit("corpus donor versions differ from the pinned baseline")

    expected = {item["path"]: item for item in provenance["files"]}
    actual = {str(path.relative_to(root)): path for path in corpus_files(root)}
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    changed = sorted(path for path in expected.keys() & actual.keys() if digest(actual[path]) != expected[path]["sha256"])
    if missing or extra or changed:
        raise SystemExit(f"corpus integrity failure: missing={missing}, extra={extra}, changed={changed}")

    for relative, item in expected.items():
        donor, source = source_for(Path(relative))
        if item["donor"] != donor or item["source"] != source:
            raise SystemExit(f"incorrect provenance mapping for {relative}")
    print(f"verified {len(expected)} immutable donor files")


def matrix_markdown(root: Path) -> str:
    matrix = json.loads((root / "compatibility.json").read_text(encoding="utf-8"))
    lines = [
        "<!-- contract-compatibility:start -->",
        "## Compatibility Matrix",
        "",
        "This section is generated from `tests/corpus/compatibility.json`. `Verified`",
        "means an automated acceptance gate exists and passes for the pinned donor.",
        "",
        "### SDK Surface",
        "",
        "| Area | Status | Evidence |",
        "|---|---|---|",
    ]
    for item in matrix["sdk"]:
        lines.append(f"| {item['area']} | {item['status']} | `{item['evidence']}` |")
    lines.extend(
        [
            "",
            "### Unchanged Contracts",
            "",
            "| Contract | Source | Build | ABI | WASM | VM | Behavior | Evidence |",
            "|---|---|---|---|---|---|---|---|",
        ]
    )
    gate_keys = ("source_integrity", "unchanged_build", "abi_parity", "wasm_validation", "vm_execution", "behavior_parity")
    for item in matrix["contracts"]:
        gates = item["gates"]
        evidence = "<br>".join(f"`{value}`" for value in item["evidence"]) or "-"
        lines.append(
            f"| {item['name']} | " + " | ".join(gates[key] for key in gate_keys) + f" | {evidence} |"
        )
    lines.extend(
        [
            "",
            "The compatibility denominator covers SDK-owned source, ABI, wire, import",
            "and contract-observable behavior. Controller, consensus, fork choice and",
            "production blockchain host policy are out of scope.",
            "<!-- contract-compatibility:end -->",
        ]
    )
    return "\n".join(lines)


def update_readme(root: Path, check: bool) -> None:
    readme = root.parents[1] / "README.md"
    text = readme.read_text(encoding="utf-8")
    start = "<!-- contract-compatibility:start -->"
    end = "<!-- contract-compatibility:end -->"
    generated = matrix_markdown(root)
    if start not in text or end not in text:
        updated = text.rstrip() + "\n\n" + generated + "\n"
    else:
        prefix, remainder = text.split(start, 1)
        _, suffix = remainder.split(end, 1)
        updated = prefix + generated + suffix
    if check:
        if updated != text:
            raise SystemExit("guest/README.md compatibility matrix is stale")
    else:
        readme.write_text(updated, encoding="utf-8")


def check_matrix(root: Path) -> None:
    matrix = json.loads((root / "compatibility.json").read_text(encoding="utf-8"))
    statuses = {"Verified", "Fixture only", "Partial", "Not started", "Out of scope"}
    required = {"source_integrity", "unchanged_build", "abi_parity", "wasm_validation", "vm_execution", "behavior_parity"}
    for item in matrix["sdk"]:
        if item["status"] not in statuses:
            raise SystemExit(f"invalid SDK matrix status: {item['status']}")
    for item in matrix["contracts"]:
        if set(item["gates"]) != required:
            raise SystemExit(f"incorrect gate set for {item['name']}")
        invalid = set(item["gates"].values()) - statuses
        if invalid:
            raise SystemExit(f"invalid matrix statuses for {item['name']}: {sorted(invalid)}")
        if any(value == "Verified" for value in item["gates"].values()) and not item["evidence"]:
            raise SystemExit(f"verified contract has no automated evidence: {item['name']}")
    update_readme(root, check=True)
    print(f"verified compatibility matrix for {len(matrix['contracts'])} contracts")


def canonical_abi(value: dict) -> dict:
    value = json.loads(json.dumps(value))
    value.pop("____comment", None)
    for key in ("abi_extensions", "error_messages", "kv_tables"):
        if not value.get(key):
            value.pop(key, None)
    sort_keys = {
        "types": "new_type_name",
        "structs": "name",
        "actions": "name",
        "tables": "name",
        "ricardian_clauses": "id",
        "variants": "name",
        "action_results": "name",
    }
    for key, field in sort_keys.items():
        if key in value:
            value[key] = sorted(value[key], key=lambda item: item.get(field, ""))
    return value


def check_abi(root: Path, build_dir: Path) -> None:
    for target, relative in CONTRACTS.items():
        actual_path = build_dir / f"{target}.abi"
        if not actual_path.is_file():
            raise SystemExit(f"missing generated ABI: {actual_path}")
        actual = canonical_abi(json.loads(actual_path.read_text(encoding="utf-8")))
        if relative is None:
            collections = ("types", "structs", "actions", "tables", "variants", "action_results")
            if actual.get("version") != "eosio::abi/1.2" or any(actual.get(key) for key in collections):
                raise SystemExit(f"{target} must retain the donor's ABI-less contract shape")
            continue
        expected_path = root / relative
        expected = canonical_abi(json.loads(expected_path.read_text(encoding="utf-8")))
        if actual != expected:
            raise SystemExit(f"canonical ABI differs from pinned donor: {target}")
    print(f"verified canonical ABI parity for {len(CONTRACTS)} contracts")


def check_artifacts(build_dir: Path, intrinsics_path: Path) -> None:
    registry = json.loads(intrinsics_path.read_text(encoding="utf-8"))
    approved = {(item["module"], item["import"]) for item in registry["imports"]}
    for target in CONTRACTS:
        wasm = build_dir / f"{target}.wasm"
        abi = build_dir / f"{target}.abi"
        manifest_path = build_dir / f"{target}.contract.json"
        for path in (wasm, abi, manifest_path):
            if not path.is_file():
                raise SystemExit(f"missing contract artifact: {path}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest["schema_version"] != 3 or "source_graph" in manifest:
            raise SystemExit(f"runtime manifest schema is invalid: {target}")
        if manifest["wasm"]["sha256"] != digest(wasm):
            raise SystemExit(f"WASM hash mismatch in manifest: {target}")
        if manifest["abi"]["sha256"] != digest(abi):
            raise SystemExit(f"ABI hash mismatch in manifest: {target}")
        if manifest["wasm"]["features"] != ["mvp"]:
            raise SystemExit(f"non-MVP WASM feature profile: {target}")
        imports = [(item["module"], item["name"]) for item in manifest["wasm"]["imports"]]
        if len(imports) != len(set(imports)):
            raise SystemExit(f"duplicate WASM import: {target}")
        unknown = sorted(set(imports) - approved)
        if unknown:
            raise SystemExit(f"unapproved WASM imports for {target}: {unknown}")
    print(f"verified hashes, imports and WASM features for {len(CONTRACTS)} contracts")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "command", choices=("write-provenance", "integrity", "write-readme", "matrix", "abi", "artifacts")
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--intrinsics", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.command == "write-provenance":
        write_provenance(root)
    elif args.command == "integrity":
        check_integrity(root)
    elif args.command == "write-readme":
        update_readme(root, check=False)
    elif args.command == "abi":
        if args.build_dir is None:
            parser.error("abi requires --build-dir")
        check_abi(root, args.build_dir.resolve())
    elif args.command == "artifacts":
        if args.build_dir is None or args.intrinsics is None:
            parser.error("artifacts requires --build-dir and --intrinsics")
        check_artifacts(args.build_dir.resolve(), args.intrinsics.resolve())
    else:
        check_matrix(root)


if __name__ == "__main__":
    main()

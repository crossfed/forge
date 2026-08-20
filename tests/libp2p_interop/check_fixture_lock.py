#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from provenance import graph_hash, sha256_file


EXPECTED_TOOLCHAINS = {
    "go": {
        "version": "1.26.3",
        "gotoolchain": "local",
        "goproxy": "off",
        "gosumdb": "off",
    },
    "rust": {
        "rustc_version": "1.95.0",
        "cargo_version": "1.95.0",
        "cargo_frozen": True,
        "cargo_net_offline": True,
    },
    "forge_fixture": {
        "compiler_identity": "embedded_build_info",
    },
}


def git_value(path: Path, revision: str) -> str:
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", revision], text=True).strip()


def check_hashes(root: Path, label: str, values: object, errors: list[str]) -> None:
    if not isinstance(values, dict):
        errors.append(f"fixture lock {label} must be an object")
        return
    for relative, expected in values.items():
        if not isinstance(relative, str) or not isinstance(expected, str):
            errors.append(f"fixture lock {label} has an invalid hash entry")
            continue
        path = root / relative
        if not path.is_file():
            errors.append(f"fixture lock {label} source is missing: {relative}")
            continue
        actual = sha256_file(path)
        if actual != expected:
            errors.append(f"fixture lock {label} hash mismatch: {relative}")


def check_toolchains(values: object, errors: list[str]) -> None:
    if values != EXPECTED_TOOLCHAINS:
        errors.append("fixture lock toolchains do not match the supported exact interop baseline")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_fixture_lock.py SOURCE_DIR DONORS_ROOT", file=sys.stderr)
        return 2
    source_dir = Path(sys.argv[1]).resolve()
    donors_root = Path(sys.argv[2]).resolve()
    try:
        lock = json.loads((source_dir / "fixture-lock.json").read_text())
    except (OSError, json.JSONDecodeError) as error:
        print(f"ERROR: fixture lock: {error}", file=sys.stderr)
        return 1
    errors: list[str] = []
    if lock.get("schema_version") != 1:
        errors.append("fixture lock schema_version must be 1")
    check_hashes(source_dir, "fixture_files", lock.get("fixture_files"), errors)
    check_hashes(source_dir, "runtime_artifact_sources", lock.get("runtime_artifact_sources"), errors)
    check_hashes(source_dir, "evidence_sources", lock.get("evidence_sources"), errors)
    check_toolchains(lock.get("toolchains"), errors)

    graphs = lock.get("dependency_graphs")
    if not isinstance(graphs, dict):
        errors.append("fixture lock dependency_graphs must be an object")
    else:
        for name, value in graphs.items():
            if not isinstance(name, str) or not isinstance(value, dict):
                errors.append("fixture lock has an invalid dependency graph")
                continue
            paths = value.get("files")
            expected = value.get("sha256")
            if not isinstance(paths, list) or not all(isinstance(path, str) for path in paths) or not isinstance(expected, str):
                errors.append(f"fixture lock dependency graph {name} is invalid")
                continue
            if graph_hash(source_dir, paths) != expected:
                errors.append(f"fixture lock dependency graph hash mismatch: {name}")

    donors = lock.get("donors")
    if not isinstance(donors, list) or len(donors) != 5:
        errors.append("fixture lock must contain the five pinned donors")
    else:
        expected_names = {"go-libp2p", "rust-libp2p", "go-kad", "go-pubsub", "libp2p-specs"}
        seen_names = set()
        for donor in donors:
            if not isinstance(donor, dict):
                errors.append("fixture lock donor entry must be an object")
                continue
            name = donor.get("name")
            directory = donor.get("directory")
            commit = donor.get("commit")
            tree = donor.get("tree")
            if not all(isinstance(value, str) and value for value in (name, directory, commit, tree)):
                errors.append(f"fixture lock donor entry is invalid: {donor}")
                continue
            seen_names.add(name)
            checkout = donors_root / directory
            if not checkout.is_dir():
                errors.append(f"fixture donor checkout is missing: {checkout}")
                continue
            try:
                if git_value(checkout, commit) != commit:
                    errors.append(f"fixture donor commit is unavailable: {name}")
                if git_value(checkout, f"{commit}^{{tree}}") != tree:
                    errors.append(f"fixture donor tree mismatch: {name}")
            except subprocess.CalledProcessError as error:
                errors.append(f"fixture donor revision lookup failed for {name}: {error}")
        if seen_names != expected_names:
            errors.append("fixture lock donor names do not match the pinned donor set")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("fixture lock ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


BOOST_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](?P<path>boost/[^>"]+)[>"]', re.MULTILINE)


def fail(message: str) -> None:
    raise SystemExit(message)


def require_files(root: Path, paths: list[str], label: str) -> None:
    missing = [path for path in paths if not (root / path).is_file()]
    if missing:
        fail(f"missing {label}: {', '.join(missing)}")


def require_tokens(path: Path, tokens: list[str], label: str) -> None:
    text = path.read_text(encoding="utf-8")
    missing = [token for token in tokens if token not in text]
    if missing:
        fail(f"missing {label} in {path}: {', '.join(missing)}")


def require_boost_header_closure(include: Path) -> None:
    missing = set()
    for header in sorted((include / "boost").rglob("*")):
        if not header.is_file():
            continue
        source = header.read_text(encoding="utf-8", errors="ignore")
        for match in BOOST_INCLUDE.finditer(source):
            dependency = match.group("path")
            if not (include / dependency).is_file():
                missing.add(dependency)
    if missing:
        fail(f"missing Boost header closure: {', '.join(sorted(missing))}")


def parse_golden(path: Path) -> list[tuple[str, str, str, str, tuple[str, ...], str]]:
    result = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name, capability, header, feature, parameters, return_type = line.split("|")
        result.append((name, capability, header, feature, tuple(filter(None, parameters.split(","))), return_type))
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--registry", required=True, type=Path)
    parser.add_argument("--golden", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    surface = json.loads(args.manifest.read_text(encoding="utf-8"))
    registry = json.loads(args.registry.read_text(encoding="utf-8"))
    registry_imports = registry["imports"]
    imports = [item for item in registry_imports if item["capability"] != "runtime"]
    expected = surface["intrinsics"]

    if len(imports) != expected["count"]:
        fail(f"intrinsic count mismatch: expected {expected['count']}, got {len(imports)}")
    if registry["interface_version"] != expected["interface_version"]:
        fail("contract interface version mismatch")
    public_capabilities = [capability for capability in registry["capabilities"] if capability != "runtime"]
    if public_capabilities != expected["capabilities"]:
        fail("contract capability set mismatch")
    if len({item["import"] for item in registry_imports}) != len(registry_imports):
        fail("duplicate intrinsic import name")

    actual_signatures = [
        (
            item["import"],
            item["capability"],
            item["header"],
            item["protocol_feature"] or "",
            tuple(item["wasm_parameters"]),
            item["wasm_result"] or "",
        )
        for item in imports
    ]
    golden_signatures = [signature for signature in parse_golden(args.golden) if signature[1] != "runtime"]
    if actual_signatures != golden_signatures:
        fail("intrinsic signatures differ from the pinned CDT/Spring golden manifest")

    include = args.data_dir / "include"
    require_boost_header_closure(include)
    require_files(include, surface["donor_c_headers"], "CDT C headers")
    require_files(include, surface["canonical_c_headers"], "canonical C headers")
    require_files(include / "eosio", surface["donor_cpp_headers"], "CDT C++ headers")
    require_files(
        args.data_dir / "modules/forge/contract",
        [f"{name}.cppm" for name in surface["modern_modules"]],
        "modern contract modules",
    )

    attribute_source = args.source_root / "libraries/contract/attributes/registry.cpp"
    require_tokens(attribute_source, [f'"{name}"' for name in surface["attributes"]], "contract attributes")

    abi_source = args.source_root / "libraries/contract/abi/generator.cpp"
    require_tokens(abi_source, [f'"{name}"' for name in surface["abi_types"]], "ABI type vocabulary")

    contract_root = args.source_root / "guest/libraries/contract/include/forge/contract"
    contract_text = "\n".join(path.read_text(encoding="utf-8") for path in sorted(contract_root.glob("*.cppm")))
    missing_errors = [message for message in surface["observable_errors"] if message not in contract_text]
    if missing_errors:
        fail(f"missing contract-visible errors: {', '.join(missing_errors)}")

    print(
        f"validated {len(imports)} intrinsics, {len(surface['donor_c_headers'])} CDT C headers, "
        f"{len(surface['donor_cpp_headers'])} CDT C++ headers, and {len(surface['modern_modules'])} modern modules"
    )


if __name__ == "__main__":
    main()

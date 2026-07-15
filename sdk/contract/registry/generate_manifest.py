#!/usr/bin/env python3

import argparse
import json
import pathlib
import re


ENTRY = re.compile(
    r"FORGE_CONTRACT_INTRINSIC\(\s*(\d+)\s*,\s*([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)\s*,\s*"
    r"([A-Za-z_]\w*)\s*,\s*([^,]+?)\s*,\s*\((.*?)\)\s*\)",
    re.DOTALL,
)


def wasm_type(cpp_type: str) -> str:
    normalized = " ".join(cpp_type.split())
    if "*" in normalized:
        return "i32"
    if normalized in {"std::uint32_t", "std::int32_t"}:
        return "i32"
    if normalized in {"std::uint64_t", "std::int64_t"}:
        return "i64"
    raise ValueError(f"intrinsic type has no WASM mapping: {normalized}")


def wasm_parameters(parameters: str) -> list[str]:
    normalized = " ".join(parameters.split())
    if not normalized:
        return []

    result = []
    for parameter in normalized.split(","):
        declaration = parameter.strip()
        type_name, _, _ = declaration.rpartition(" ")
        if not type_name:
            raise ValueError(f"intrinsic parameter has no name: {declaration}")
        result.append(wasm_type(type_name))
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    source = args.registry.read_text(encoding="utf-8")
    entries = []
    for match in ENTRY.finditer(source):
        version, identifier, module, import_name, result, parameters = match.groups()
        normalized_result = " ".join(result.split())
        normalized_parameters = " ".join(parameters.split())
        entries.append(
            {
                "interface_version": int(version),
                "identifier": identifier,
                "module": module,
                "import": import_name,
                "result": normalized_result,
                "parameters": normalized_parameters,
                "wasm_result": None if normalized_result == "void" else wasm_type(normalized_result),
                "wasm_parameters": wasm_parameters(normalized_parameters),
            }
        )

    if not entries:
        raise SystemExit("intrinsic registry contains no declarations")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "interface_version": max(entry["interface_version"] for entry in entries),
                "imports": entries,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()

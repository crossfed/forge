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


def c_type(value: str) -> str:
    return (
        " ".join(value.split())
        .replace("std::uint32_t", "uint32_t")
        .replace("std::int32_t", "int32_t")
        .replace("std::uint64_t", "uint64_t")
        .replace("std::int64_t", "int64_t")
    )


def write_c_header(path: pathlib.Path, entries: list[dict]) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "#if defined(__clang__) && __has_attribute(import_module)",
        "#define FORGE_CONTRACT_IMPORT(module, name) \\",
        "   __attribute__((import_module(module), import_name(name)))",
        "#else",
        "#define FORGE_CONTRACT_IMPORT(module, name)",
        "#endif",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    for entry in entries:
        result = c_type(entry["result"])
        parameters = c_type(entry["parameters"]) or "void"
        lines.append(
            f'{result} {entry["identifier"]}({parameters}) '
            f'FORGE_CONTRACT_IMPORT("{entry["module"]}", "{entry["import"]}");'
        )
    lines.extend(
        [
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            "#undef FORGE_CONTRACT_IMPORT",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_eosio_header(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#pragma once\n\n#include <forge/contract/intrinsics.h>\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--forge-header", type=pathlib.Path)
    parser.add_argument("--eosio-header", type=pathlib.Path)
    parser.add_argument("--eosio-db-header", type=pathlib.Path)
    args = parser.parse_args()

    source = args.registry.read_text(encoding="utf-8").replace("\\\n", " ")
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
    if args.forge_header is not None:
        write_c_header(args.forge_header, entries)
    if args.eosio_header is not None:
        write_eosio_header(args.eosio_header)
    if args.eosio_db_header is not None:
        write_eosio_header(args.eosio_db_header)


if __name__ == "__main__":
    main()

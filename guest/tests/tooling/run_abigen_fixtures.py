#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys


MODULES = (
    "forge.raw.stream",
    "forge.raw.varint_value",
    "forge.raw.codec",
    "forge.chain.protocol.values",
    "forge.contract.intrinsics",
    "forge.contract",
    "forge.contract.dispatcher",
)

MODULE_TARGETS = {
    "forge.raw.stream": "forge_guest_raw",
    "forge.raw.varint_value": "forge_guest_raw",
    "forge.raw.codec": "forge_guest_raw",
    "forge.chain.protocol.values": "forge_guest_chain_protocol",
    "forge.contract.intrinsics": "forge_guest_contract",
    "forge.contract": "forge_guest_contract",
    "forge.contract.dispatcher": "forge_guest_contract",
}

PASS_FIXTURES = {
    "action_results_test": "action_results_test",
    "aliased_type_variant_template_arg": "aliased_type_variant_template_arg",
    "nested_container": "nested_container",
    "ricardian_contract_test": "ricardian_contract_test",
    "singleton_contract": "singleton_contract",
    "struct_base_typedefd": "struct_base_typedefd",
    "sync_call_test": "sync_call_test",
    "tagged_number_test": "tagged_number_test",
    "using_std_array": "using_std_array",
}

FAIL_FIXTURES = {
    "empty_contract": "hello",
    "empty_contract_with_other_contract": "hello",
    "wrong_contract_name": "nothello",
}


def invoke(args, contract, source, output, *, succeeds=True, ricardian_contracts=None, ricardian_clauses=None):
    output.mkdir(parents=True, exist_ok=True)
    abi = output / f"{source.stem}.abi"
    dispatch = output / f"{source.stem}.dispatcher.cpp"
    command = [
        str(args.abigen),
        "--contract",
        contract,
        "--abi",
        str(abi),
        "--dispatch",
        str(dispatch),
        "--attribute-plugin",
        str(args.plugin),
        "--sysroot",
        str(args.sysroot),
        "--include",
        str(args.include),
    ]
    for module in MODULES:
        module_dir = args.build_dir / "CMakeFiles" / f"{MODULE_TARGETS[module]}.dir"
        command.extend(("--module-file", f"{module}={module_dir / (module + '.pcm')}"))
    if ricardian_contracts is not None:
        command.extend(("--ricardian-contracts", str(ricardian_contracts)))
    if ricardian_clauses is not None:
        command.extend(("--ricardian-clauses", str(ricardian_clauses)))
    command.append(str(source))

    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if succeeds and result.returncode != 0:
        raise RuntimeError(f"abigen failed for {source.name}:\n{result.stdout}{result.stderr}")
    if not succeeds and result.returncode == 0:
        raise RuntimeError(f"abigen unexpectedly accepted {source.name}")
    if not succeeds:
        return None
    return json.loads(abi.read_text(encoding="utf-8"))


def by_name(values):
    return {value["name"]: value for value in values}


def check_features(abi):
    assert abi["version"] == "eosio::abi/1.3"
    assert {entry["new_type_name"]: entry["type"] for entry in abi["types"]} == {
        "B_map_string_string_E": "pair_string_string[]",
        "B_vector_int32_E": "int32[]",
        "base_alias": "base_value",
        "str": "string",
    }

    structs = by_name(abi["structs"])
    assert structs["derived_value"]["base"] == "base_alias"
    assert structs["tagged_number_3472950412842106880"]["fields"] == [
        {"name": "value", "type": "uint64"}
    ]
    assert by_name(structs["nested"]["fields"])["arg2"]["type"] == "uint32?"
    assert "pair_string_B_map_string_string_E" in structs
    assert "tuple_int32_float64_string_B_vector_int32_E" in structs

    variants = by_name(abi["variants"])
    assert variants["variant_uint64_str"]["types"] == ["uint64", "str"]
    tables = by_name(abi["tables"])
    assert tables["records"]["type"] == "record"
    assert tables["indextable"]["type"] == "indexed_record"
    assert tables["singletn"]["type"] == "indexed_record"
    assert by_name(abi["action_results"])["result"]["result_type"] == "result_value"
    assert by_name(abi["calls"])["sum"] == {
        "name": "sum",
        "type": "sum",
        "id": 193506202,
        "result_type": "uint32",
    }
    assert by_name(abi["actions"])["alias"]["ricardian_contract"].endswith(
        "This action is a donor-derived ABI fixture."
    )
    clauses = {entry["id"]: entry for entry in abi["ricardian_clauses"]}
    assert clauses["Compatibility"]["body"] == (
        "This clause records Spring and CDT compatibility intent."
    )


def normalize_abi(value):
    value = dict(value)
    value.pop("____comment", None)
    value.setdefault("error_messages", [])
    value.setdefault("abi_extensions", [])
    value.setdefault("variants", [])
    value.setdefault("action_results", [])
    return value


def check_fixture_semantics(name, abi):
    structs = by_name(abi["structs"])
    actions = by_name(abi["actions"])
    tables = by_name(abi["tables"])
    variants = by_name(abi["variants"])
    results = by_name(abi["action_results"])
    checks = {
        "action_results_test": lambda: results["action5"]["result_type"] == "test_res",
        "aliased_type_variant_template_arg": lambda: variants["variant_uint64_str"]["types"] == ["uint64", "str"],
        "nested_container": lambda: structs["settuple2"]["fields"][1]["type"]
        == "tuple_int32_float64_string_B_vector_int32_E",
        "ricardian_contract_test": lambda: actions["test"]["ricardian_contract"].startswith("---\nspec-version"),
        "singleton_contract": lambda: set(tables) == {"config", "config55", "mi.config52", "smpl.conf5", "smpl.config"},
        "struct_base_typedefd": lambda: structs["baz"]["base"] == "bar",
        "sync_call_test": lambda: by_name(abi["calls"])["sum"]["id"] == 193506202,
        "tagged_number_test": lambda: "TaggedNumber_3472950412842106880" in structs,
        "using_std_array": lambda: tables["greeting"]["type"] == "greeting",
    }
    if not checks[name]():
        raise RuntimeError(f"CDT donor fixture semantics changed: {name}")


def run_donor_fixtures(args, manifest):
    donor_root = os.environ.get("FORGE_CONTRACT_CDT_DONOR")
    donor_root = pathlib.Path(donor_root) if donor_root else None
    local_root = args.fixtures / "cdt"

    for name, contract in PASS_FIXTURES.items():
        source = local_root / "abigen-pass" / f"{name}.cpp"
        kwargs = {}
        if name == "ricardian_contract_test":
            kwargs = {
                "ricardian_contracts": source.with_suffix(".contracts.md"),
                "ricardian_clauses": source.with_suffix(".clauses.md"),
            }
        abi = invoke(args, contract, source, args.output / "cdt", **kwargs)
        check_fixture_semantics(name, abi)
        if donor_root is not None:
            expected_path = donor_root / "tests/toolchain/abigen-pass" / f"{name}.abi"
            expected = json.loads(expected_path.read_text(encoding="utf-8"))
            if normalize_abi(abi) != normalize_abi(expected):
                raise RuntimeError(f"Forge ABI differs from CDT donor fixture: {name}")

    for name, contract in FAIL_FIXTURES.items():
        source = local_root / "abigen-fail" / f"{name}.cpp"
        invoke(args, contract, source, args.output / "cdt", succeeds=False)

    mapped = {fixture["forge_case"] for fixture in manifest["fixtures"]}
    expected = {
        f"cdt/abigen-pass/{name}.cpp" for name in PASS_FIXTURES
    } | {
        f"cdt/abigen-fail/{name}.cpp" for name in FAIL_FIXTURES
    }
    if mapped != expected:
        raise RuntimeError("CDT donor fixture mapping does not match the executable suite")


def check_donor_manifest(fixtures):
    manifest = json.loads((fixtures / "donor_manifest.json").read_text(encoding="utf-8"))
    if manifest["donor"]["commit"] != "69599db279b7b93d0688502720c15c6962a1401b":
        raise RuntimeError("unexpected CDT donor commit")
    if len(manifest["fixtures"]) != 12:
        raise RuntimeError("CDT donor mapping is incomplete")

    donor_root = os.environ.get("FORGE_CONTRACT_CDT_DONOR")
    if not donor_root:
        return manifest
    donor_root = pathlib.Path(donor_root)
    for fixture in manifest["fixtures"]:
        source = donor_root / fixture["path"]
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if digest != fixture["sha256"]:
            raise RuntimeError(f"CDT donor fixture hash changed: {fixture['path']}")
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--abigen", required=True, type=pathlib.Path)
    parser.add_argument("--plugin", required=True, type=pathlib.Path)
    parser.add_argument("--sysroot", required=True, type=pathlib.Path)
    parser.add_argument("--include", required=True, type=pathlib.Path)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--fixtures", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = check_donor_manifest(args.fixtures)
    run_donor_fixtures(args, manifest)

    features = invoke(
        args,
        "abifixture",
        args.fixtures / "abigen_features.cpp",
        args.output,
        ricardian_contracts=args.fixtures / "ricardian.contracts.md",
        ricardian_clauses=args.fixtures / "ricardian.clauses.md",
    )
    check_features(features)

    modern = invoke(args, "parity", args.fixtures / "parity_modern.cpp", args.output)
    legacy = invoke(args, "parity", args.fixtures / "parity_legacy.cpp", args.output)
    if modern != legacy:
        raise RuntimeError("forge and eosio attribute spellings produced different canonical ABI")

    invoke(args, "empty", args.fixtures / "empty_contract.cpp", args.output, succeeds=False)
    invoke(args, "missing", args.fixtures / "wrong_contract.cpp", args.output, succeeds=False)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"run_abigen_fixtures: {error}", file=sys.stderr)
        raise

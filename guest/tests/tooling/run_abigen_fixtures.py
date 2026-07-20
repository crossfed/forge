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
    "forge.crypto.sha256:value",
    "forge.crypto.sha256",
    "forge.crypto.sha512:value",
    "forge.crypto.sha512",
    "forge.crypto.ripemd160:value",
    "forge.crypto.ripemd160",
    "forge.crypto.asymmetric:value",
    "forge.crypto.asymmetric",
    "forge.crypto.bls.values",
    "forge.chain.protocol.values",
    "forge.chain.protocol.time",
    "forge.chain.protocol.types:value",
    "forge.chain.protocol.types",
    "forge.chain.protocol.fixed_key:value",
    "forge.chain.protocol.fixed_key",
    "forge.chain.protocol.action:value",
    "forge.chain.protocol.action",
    "forge.chain.protocol.transaction:value",
    "forge.chain.protocol.transaction",
    "forge.chain.protocol.authority:value",
    "forge.chain.protocol.authority",
    "forge.chain.protocol.producer_authority",
    "forge.chain.protocol.system:value",
    "forge.chain.protocol.system",
    "forge.contract.intrinsics",
    "forge.contract",
    "forge.contract.datastream",
    "forge.contract.varint",
    "forge.contract.fixed_bytes",
    "forge.contract.binary_extension",
    "forge.contract.ignore",
    "forge.contract.hash_id",
    "forge.contract.action",
    "forge.contract.base64",
    "forge.contract.transaction",
    "forge.contract.system",
    "forge.contract.deferred_transaction",
    "forge.contract.authorization",
    "forge.contract.bitset",
    "forge.contract.call",
    "forge.contract.crypto",
    "forge.contract.crypto_bls_ext",
    "forge.contract.crypto_ext",
    "forge.contract.instant_finality",
    "forge.contract.key",
    "forge.contract.powers",
    "forge.contract.print",
    "forge.contract.privileged",
    "forge.contract.producer_schedule",
    "forge.contract.rope",
    "forge.contract.string",
    "forge.contract.dispatcher",
    "forge.contract.multi_index",
    "forge.contract.singleton",
    "forge.contract.compatibility_asset",
)

MODULE_TARGETS = {
    module: (
        "forge_guest_raw"
        if module.startswith("forge.raw.")
        else "forge_guest_crypto"
        if module.startswith("forge.crypto.")
        else "forge_guest_chain_protocol"
        if module.startswith("forge.chain.protocol.")
        else "forge_guest_contract"
    )
    for module in MODULES
}

MODULE_FILES = {
    "forge.crypto.sha256:value": "forge.crypto.sha256-value.pcm",
    "forge.crypto.sha512:value": "forge.crypto.sha512-value.pcm",
    "forge.crypto.ripemd160:value": "forge.crypto.ripemd160-value.pcm",
    "forge.crypto.asymmetric:value": "forge.crypto.asymmetric-value.pcm",
    "forge.chain.protocol.types:value": "forge.chain.protocol.types-value.pcm",
    "forge.chain.protocol.fixed_key:value": "forge.chain.protocol.fixed_key-value.pcm",
    "forge.chain.protocol.action:value": "forge.chain.protocol.action-value.pcm",
    "forge.chain.protocol.transaction:value": "forge.chain.protocol.transaction-value.pcm",
    "forge.chain.protocol.authority:value": "forge.chain.protocol.authority-value.pcm",
    "forge.chain.protocol.system:value": "forge.chain.protocol.system-value.pcm",
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


def invoke(
    args,
    contract,
    source,
    output,
    *,
    succeeds=True,
    additional_sources=(),
    ricardian_contracts=None,
    ricardian_clauses=None,
    bare_outputs=False,
    error_contains=None,
):
    output.mkdir(parents=True, exist_ok=True)
    abi = output / f"{source.stem}.abi"
    dispatch = output / f"{source.stem}.dispatcher.cpp"
    abi_argument = abi.name if bare_outputs else abi
    dispatch_argument = dispatch.name if bare_outputs else dispatch
    command = [
        str(args.abigen),
        "--contract",
        contract,
        "--abi",
        str(abi_argument),
        "--dispatch",
        str(dispatch_argument),
        "--attribute-plugin",
        str(args.plugin),
        "--sysroot",
        str(args.sysroot),
        "--include",
        str(args.include),
    ]
    for module in MODULES:
        module_dir = args.build_dir / "CMakeFiles" / f"{MODULE_TARGETS[module]}.dir"
        module_file = MODULE_FILES.get(module, module + ".pcm")
        command.extend(("--module-file", f"{module}={module_dir / module_file}"))
    if ricardian_contracts is not None:
        command.extend(("--ricardian-contracts", str(ricardian_contracts)))
    if ricardian_clauses is not None:
        command.extend(("--ricardian-clauses", str(ricardian_clauses)))
    source_wrappers = []
    for index, _ in enumerate(additional_sources, start=1):
        wrapper = output / f"{source.stem}.source-{index}.cpp"
        source_wrappers.append(wrapper)
        command.extend(("--source-wrapper", str(wrapper)))
    command.extend((str(source), *(str(item) for item in additional_sources)))

    result = subprocess.run(
        command,
        cwd=output if bare_outputs else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if succeeds and result.returncode != 0:
        raise RuntimeError(f"abigen failed for {source.name}:\n{result.stdout}{result.stderr}")
    if not succeeds and result.returncode == 0:
        raise RuntimeError(f"abigen unexpectedly accepted {source.name}")
    if not succeeds:
        diagnostics = result.stdout + result.stderr
        if error_contains is not None and error_contains not in diagnostics:
            raise RuntimeError(
                f"abigen rejected {source.name} without expected diagnostic {error_contains!r}:\n{diagnostics}"
            )
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
        "my_account": "name",
        "signed_int": "varint32",
        "str": "string",
        "unsigned_int": "varuint32",
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
    assert set(tables) == {"defaultrec", "indextable", "owned", "records", "singletn", "varints"}
    assert tables["defaultrec"]["type"] == "defaultrec"
    assert tables["records"]["type"] == "record"
    assert tables["owned"]["type"] == "owned_record"
    assert tables["indextable"]["type"] == "indexed_record"
    assert tables["singletn"]["type"] == "indexed_record"
    assert tables["varints"]["type"] == "varint_record"
    assert by_name(structs["varint_record"]["fields"])["unsigned_value"]["type"] == "varuint32"
    assert by_name(structs["varint_record"]["fields"])["signed_value"]["type"] == "varint32"
    varint_fields = by_name(structs["varintargs"]["fields"])
    assert varint_fields["unsigned_value"]["type"] == "unsigned_int"
    assert varint_fields["signed_value"]["type"] == "signed_int"
    assert by_name(structs["extension"]["fields"])["value"]["type"] == "uint32$"
    assert by_name(structs["named"]["fields"])["owner"]["type"] == "my_account"
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

    guest_macro = invoke(
        args,
        "guestmacro",
        args.fixtures.parent / "consumer" / "guest_macro_contract.cpp",
        args.output / "guest-macro",
    )
    if [action["name"] for action in guest_macro["actions"]] != ["run"]:
        raise RuntimeError("abigen did not analyze the guest contract translation unit")

    implicit_contract = invoke(
        args,
        "implicit_contract",
        args.fixtures / "implicit_contract_table.cpp",
        args.output / "implicit-contract-table",
    )
    if [table["name"] for table in implicit_contract["tables"]] != ["indextable"]:
        raise RuntimeError("abigen did not infer a table owned by an implicitly named contract")

    multi_source_contract = args.fixtures / "multi_source_contract.cpp"
    multi_source_helper = args.fixtures / "multi_source_helper.cpp"
    invoke(
        args,
        "multisource",
        args.fixtures / "empty_contract.cpp",
        args.output / "multi-source-invalid",
        succeeds=False,
        additional_sources=(multi_source_contract,),
    )
    multi_source = invoke(
        args,
        "multisource",
        multi_source_contract,
        args.output / "multi-source-valid",
        additional_sources=(multi_source_helper,),
    )
    if [action["name"] for action in multi_source["actions"]] != ["next", "previous"]:
        raise RuntimeError("shared multi-source action was not de-duplicated")
    helper_wrapper = args.output / "multi-source-valid/multi_source_contract.source-1.cpp"
    if "__forge_contract_action_" not in helper_wrapper.read_text(encoding="utf-8"):
        raise RuntimeError("secondary translation-unit action did not receive a dispatch thunk")

    user_vector = invoke(
        args,
        "uservector",
        args.fixtures / "user_vector_template.cpp",
        args.output / "user-vector-template",
    )
    user_vector_structs = by_name(user_vector["structs"])
    if user_vector_structs["store"]["fields"][0]["type"] != "vector_uint32":
        raise RuntimeError("user-defined vector template was encoded as a standard ABI array")
    if user_vector_structs["vector_uint32"]["fields"] != [{"name": "value", "type": "uint32"}]:
        raise RuntimeError("user-defined vector template record shape changed")

    eosio_transaction = invoke(
        args,
        "transactionabi",
        args.fixtures / "eosio_transaction_abi.cpp",
        args.output / "eosio-transaction-abi",
    )
    transaction_structs = [item for item in eosio_transaction["structs"] if item["name"] == "transaction"]
    if len(transaction_structs) != 1:
        raise RuntimeError("EOSIO transaction adapter did not reuse the canonical transaction ABI record")
    transaction_records = by_name(eosio_transaction["structs"])
    if transaction_records["permission_level"]["fields"] != [
        {"name": "actor", "type": "name"},
        {"name": "permission", "type": "name"},
    ]:
        raise RuntimeError("EOSIO transaction adapter changed the canonical permission_level ABI record")
    if transaction_records["action"] != {
        "name": "action",
        "base": "",
        "fields": [
            {"name": "account", "type": "name"},
            {"name": "name", "type": "name"},
            {"name": "authorization", "type": "permission_level[]"},
            {"name": "data", "type": "bytes"},
        ],
    }:
        raise RuntimeError("EOSIO transaction adapter leaked its C++ inheritance into the ABI")
    if transaction_records["extension"]["fields"] != [
        {"name": "type", "type": "uint16"},
        {"name": "data", "type": "bytes"},
    ]:
        raise RuntimeError("EOSIO transaction adapter changed the canonical extension ABI record")
    if by_name(eosio_transaction["actions"])["sendtrx"]["type"] != "submit":
        raise RuntimeError("annotated EOSIO action name replaced its donor method ABI type")
    submit_fields = by_name(by_name(eosio_transaction["structs"])["submit"]["fields"])
    if submit_fields["value"]["type"] != "transaction":
        raise RuntimeError("EOSIO transaction action argument changed ABI type")

    eosio_fixed_bytes = invoke(
        args,
        "fixedbytes",
        args.fixtures / "eosio_fixed_bytes_abi.cpp",
        args.output / "eosio-fixed-bytes-abi",
    )
    fixed_bytes_fields = by_name(by_name(eosio_fixed_bytes["structs"])["verify"]["fields"])
    if {name: field["type"] for name, field in fixed_bytes_fields.items()} != {
        "one": "checksum160",
        "two": "checksum256",
        "three": "checksum512",
    }:
        raise RuntimeError("EOSIO fixed_bytes adapters changed their canonical ABI names")
    if any(item["name"].startswith("fixed_bytes") for item in eosio_fixed_bytes["structs"]):
        raise RuntimeError("EOSIO fixed_bytes adapter leaked an implementation record into the ABI")

    equivalent_struct = invoke(
        args,
        "equivalent",
        args.fixtures / "equivalent_struct.cpp",
        args.output / "equivalent-struct",
    )
    payload_structs = [item for item in equivalent_struct["structs"] if item["name"] == "payload"]
    if len(payload_structs) != 1 or payload_structs[0]["fields"] != [{"name": "value", "type": "uint32"}]:
        raise RuntimeError("equivalent ABI records were not de-duplicated")

    invoke(args, "duplicate", args.fixtures / "duplicate_action.cpp", args.output / "duplicate-action", succeeds=False)
    invoke(args, "overloaded", args.fixtures / "overloaded_action.cpp", args.output / "overloaded-action", succeeds=False)
    invoke(args, "duplicate", args.fixtures / "duplicate_struct.cpp", args.output / "duplicate-struct", succeeds=False)
    invoke(args, "duplicate", args.fixtures / "duplicate_table.cpp", args.output / "duplicate-table", succeeds=False)
    invoke(
        args,
        "anonymous",
        args.fixtures / "anonymous_record.cpp",
        args.output / "anonymous-record",
        succeeds=False,
        error_contains="anonymous or local scope",
    )
    invoke(
        args,
        "duplicateindex",
        args.fixtures / "duplicate_index_name.cpp",
        args.output / "duplicate-index-name",
        succeeds=False,
        error_contains="invalid index name used in multi_index",
    )
    invoke(
        args,
        "aliasclash",
        args.fixtures / "conflicting_type_alias.cpp",
        args.output / "conflicting-type-alias",
        succeeds=False,
    )
    invoke(
        args,
        "foreignscope",
        args.fixtures / "foreign_contract_attribute.cpp",
        args.output / "foreign-contract-attribute",
        succeeds=False,
    )
    invoke(
        args,
        "foreignscope",
        args.fixtures / "foreign_action_attribute.cpp",
        args.output / "foreign-action-attribute",
        succeeds=False,
    )
    invoke(
        args,
        "unnamedaction",
        args.fixtures / "unnamed_action_parameter.cpp",
        args.output / "unnamed-action-parameter",
        succeeds=False,
    )
    invoke(
        args,
        "unnamedcall",
        args.fixtures / "unnamed_call_parameter.cpp",
        args.output / "unnamed-call-parameter",
        succeeds=False,
    )

    modern = invoke(args, "parity", args.fixtures / "parity_modern.cpp", args.output)
    legacy = invoke(args, "parity", args.fixtures / "parity_legacy.cpp", args.output)
    if modern != legacy:
        raise RuntimeError("forge and eosio attribute spellings produced different canonical ABI")
    modern_dispatcher = (args.output / "parity_modern.dispatcher.cpp").read_text(encoding="utf-8")
    if not modern_dispatcher.startswith("#include <cstdint>\n"):
        raise RuntimeError("generated dispatcher does not declare its fixed-width integer dependency")

    for fixture in ("static_apply", "cpp_linkage_apply"):
        invoke(args, fixture, args.fixtures / f"{fixture}.cpp", args.output)
        dispatcher = (args.output / f"{fixture}.dispatcher.cpp").read_text(encoding="utf-8")
        if 'extern "C" [[gnu::visibility("default")]] void apply(' not in dispatcher:
            raise RuntimeError(f"{fixture} suppressed the generated contract dispatcher")

    invoke(args, "custom_apply", args.fixtures / "custom_apply.cpp", args.output)
    custom_dispatcher = (args.output / "custom_apply.dispatcher.cpp").read_text(encoding="utf-8")
    if 'extern "C" [[gnu::visibility("default")]] void apply(' in custom_dispatcher:
        raise RuntimeError("exported custom apply received a duplicate generated dispatcher")
    if "codec_traits<::custom_record>" not in custom_dispatcher:
        raise RuntimeError("exported custom apply did not receive generated record codecs")
    custom_apply_only = invoke(
        args,
        "custom_apply_only",
        args.fixtures / "custom_apply_only.cpp",
        args.output / "custom-apply-only",
    )
    if any(custom_apply_only[key] for key in ("types", "structs", "actions", "tables")):
        raise RuntimeError("custom apply without ABI declarations generated spurious ABI entries")
    invoke(
        args,
        "invalid_exported_apply",
        args.fixtures / "invalid_exported_apply.cpp",
        args.output,
        succeeds=False,
    )

    bare_output = args.output / "bare-output"
    invoke(
        args,
        "parity",
        args.fixtures / "parity_modern.cpp",
        bare_output,
        bare_outputs=True,
    )
    if not (bare_output / "parity_modern.abi").is_file():
        raise RuntimeError("abigen did not write a bare ABI output path")
    if not (bare_output / "parity_modern.dispatcher.cpp").is_file():
        raise RuntimeError("abigen did not write a bare dispatcher output path")

    invoke(args, "empty", args.fixtures / "empty_contract.cpp", args.output, succeeds=False)
    invoke(args, "missing", args.fixtures / "wrong_contract.cpp", args.output, succeeds=False)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"run_abigen_fixtures: {error}", file=sys.stderr)
        raise

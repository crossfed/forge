#!/usr/bin/env python3

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


def run(command, *, succeeds, contains=None, cwd=None):
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    output = result.stdout + result.stderr
    if succeeds and result.returncode != 0:
        raise RuntimeError(f"command failed:\n{' '.join(map(str, command))}\n{output}")
    if not succeeds and result.returncode == 0:
        raise RuntimeError(f"command unexpectedly succeeded:\n{' '.join(map(str, command))}")
    if contains and contains not in output:
        raise RuntimeError(f"expected diagnostic {contains!r} was not emitted:\n{output}")


def check(args, wasm, abi, imports, *, succeeds, contains=None, required_export="apply"):
    run(
        [
            str(args.checker),
            "--wasm",
            str(wasm),
            "--abi",
            str(abi),
            "--imports",
            str(imports),
            "--required-export",
            required_export,
        ],
        succeeds=succeeds,
        contains=contains,
    )


def compile_wasm(args, source, output, *flags):
    run(
        [
            str(args.clang),
            "--target=wasm32",
            "-nostdlib",
            "-fno-ident",
            "-Wl,--no-entry",
            "-Wl,--export=apply",
            "-Wl,--allow-undefined",
            *flags,
            str(source),
            "-o",
            str(output),
        ],
        succeeds=True,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checker", required=True, type=pathlib.Path)
    parser.add_argument("--manifest-tool", required=True, type=pathlib.Path)
    parser.add_argument("--clang", required=True, type=pathlib.Path)
    parser.add_argument("--wasm", required=True, type=pathlib.Path)
    parser.add_argument("--abi", required=True, type=pathlib.Path)
    parser.add_argument("--imports", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    assert manifest["schema_version"] == 1
    assert manifest["sdk"]["profile"] in {"developer", "release"}
    assert manifest["sdk"]["reproducible"] == (manifest["sdk"]["profile"] == "release")
    if manifest["sdk"]["profile"] == "release":
        assert manifest["llvm"] == {
            "version": "llvmorg-22.1.8",
            "commit": "ca7933e47d3a3451d81e72ac174dcb5aa28b59d1",
        }
    else:
        clang_version = subprocess.run(
            [str(args.clang), "--version"], check=True, text=True, stdout=subprocess.PIPE
        ).stdout.splitlines()[0]
        assert manifest["llvm"] == {"version": clang_version}
    assert manifest["wasm"]["sha256"] == hashlib.sha256(args.wasm.read_bytes()).hexdigest()
    assert manifest["abi"]["sha256"] == hashlib.sha256(args.abi.read_bytes()).hexdigest()
    assert manifest["wasm"]["features"] == ["mvp"]
    assert {entry["module"] for entry in manifest["wasm"]["imports"]} == {"env"}

    bare_output = args.output / "bare-manifest"
    bare_output.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(args.manifest_tool),
            "--wasm",
            str(args.wasm),
            "--abi",
            str(args.abi),
            "--imports",
            str(args.imports),
            "--output",
            "bare.contract.json",
            "--sdk-version",
            "test",
            "--profile",
            "developer",
            "--reproducible",
            "false",
            "--llvm-version",
            "test",
            "--sysroot-version",
            "1",
            "--sysroot-hash",
            "test",
            "--intrinsic-version",
            "1",
        ],
        cwd=bare_output,
        succeeds=True,
    )
    if not (bare_output / "bare.contract.json").is_file():
        raise RuntimeError("contract-manifest did not write a bare output path")

    check(args, args.wasm, args.abi, args.imports, succeeds=True)
    check(args, args.wasm, args.abi, args.imports, succeeds=False, contains="required contract export is missing",
          required_export="missing")

    empty_abi = args.output / "empty.abi"
    empty_abi.write_bytes(b"")
    check(args, args.wasm, empty_abi, args.imports, succeeds=False, contains="ABI is missing or empty")

    malformed_abi = args.output / "malformed.abi"
    malformed_abi.write_text('{"version": 42}', encoding="utf-8")
    check(args, args.wasm, malformed_abi, args.imports, succeeds=False, contains="contract ABI")

    malformed_wasm = args.output / "malformed.wasm"
    malformed_wasm.write_bytes(b"not wasm")
    check(args, malformed_wasm, args.abi, args.imports, succeeds=False)

    wrong_apply_source = args.output / "wrong-apply.c"
    wrong_apply_source.write_text("void apply(void) {}\n", encoding="utf-8")
    wrong_apply_wasm = args.output / "wrong-apply.wasm"
    compile_wasm(args, wrong_apply_source, wrong_apply_wasm, "-x", "c")
    check(args, wrong_apply_wasm, args.abi, args.imports, succeeds=False,
          contains="apply export has the wrong signature")

    malformed_registry = args.output / "malformed-registry.json"
    malformed_registry.write_text("{", encoding="utf-8")
    check(args, args.wasm, args.abi, malformed_registry, succeeds=False, contains="intrinsic manifest")

    registry = json.loads(args.imports.read_text(encoding="utf-8"))
    unsupported = json.loads(json.dumps(registry))
    unsupported["imports"] = [entry for entry in unsupported["imports"] if entry["import"] != "read_action_data"]
    unsupported_path = args.output / "unsupported-registry.json"
    unsupported_path.write_text(json.dumps(unsupported), encoding="utf-8")
    check(args, args.wasm, args.abi, unsupported_path, succeeds=False, contains="unsupported contract import")

    wrong_signature = json.loads(json.dumps(registry))
    for entry in wrong_signature["imports"]:
        if entry["import"] == "action_data_size":
            entry["wasm_result"] = "i64"
    wrong_signature_path = args.output / "wrong-signature-registry.json"
    wrong_signature_path.write_text(json.dumps(wrong_signature), encoding="utf-8")
    check(args, args.wasm, args.abi, wrong_signature_path, succeeds=False, contains="wrong return type")

    wrong_version = json.loads(json.dumps(registry))
    wrong_version["interface_version"] = 2
    wrong_version_path = args.output / "wrong-version-registry.json"
    wrong_version_path.write_text(json.dumps(wrong_version), encoding="utf-8")
    check(args, args.wasm, args.abi, wrong_version_path, succeeds=False, contains="unsupported schema or interface")

    wasi_source = args.output / "wasi.c"
    wasi_source.write_text(
        '__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))\n'
        'extern int fd_write(int, int, int, int);\n'
        '__attribute__((export_name("apply"))) void apply(void) { (void)fd_write(0, 0, 0, 0); }\n',
        encoding="utf-8",
    )
    wasi_wasm = args.output / "wasi.wasm"
    compile_wasm(args, wasi_source, wasi_wasm, "-x", "c")
    check(args, wasi_wasm, args.abi, args.imports, succeeds=False, contains="unsupported contract import")

    simd_source = args.output / "simd.c"
    simd_source.write_text(
        'typedef int v4si __attribute__((vector_size(16)));\n'
        'volatile v4si sink;\n'
        '__attribute__((export_name("apply"))) void apply(void) { v4si x = {1,2,3,4}; sink = x + x; }\n',
        encoding="utf-8",
    )
    simd_wasm = args.output / "simd.wasm"
    compile_wasm(args, simd_source, simd_wasm, "-x", "c", "-msimd128")
    check(args, simd_wasm, args.abi, args.imports, succeeds=False)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"run_checker_fixtures: {error}", file=sys.stderr)
        raise

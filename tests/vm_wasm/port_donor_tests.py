#!/usr/bin/env python3
"""Reproduce the Boost.Test port from the pinned EOS VM donor tests."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import sys
import tempfile


DONOR_COMMIT = "e5b1fc79c4b8d78f32749afa94a8d4c4d071f67f"
FIXTURE_COMMIT = "ffbbb552e6020623d8ae148d81a152c8ef700325"

UNIT_SOURCES = (
    "allocator_tests.cpp",
    "eosio_max_nested_structures_tests.cpp",
    "guarded_ptr_tests.cpp",
    "varint_tests.cpp",
    "variant_tests.cpp",
    "host_functions_tests.cpp",
    "preconditions_tests.cpp",
    "allow_code_after_function_end_tests.cpp",
    "allow_invalid_empty_local_set_tests.cpp",
    "allow_u32_limits_flags_tests.cpp",
    "allow_zero_blocktype_tests.cpp",
    "forbid_export_mutable_globals_tests.cpp",
    "max_br_table_elements_tests.cpp",
    "max_code_bytes_tests.cpp",
    "max_func_local_bytes_tests.cpp",
    "max_linear_memory_init_tests.cpp",
    "max_local_sets_tests.cpp",
    "max_memory_offset_tests.cpp",
    "max_mutable_globals_tests.cpp",
    "max_nested_structures_tests.cpp",
    "max_pages_tests.cpp",
    "max_section_elements_tests.cpp",
    "max_table_elements_tests.cpp",
    "null_tests.cpp",
    "reentry_tests.cpp",
    "signals_tests.cpp",
    "stack_restriction_tests.cpp",
    "watchdog_tests.cpp",
    "implementation_limits_tests.cpp",
    "instantiation_tests.cpp",
    "backend_tests.cpp",
    "vector_tests.cpp",
)

SPEC_SOURCES = (
    "address", "align", "binary", "binary-leb128", "block", "br", "br_if", "br_table",
    "break-drop", "call_indirect", "call", "const", "conversions", "custom", "data", "elem",
    "endianness", "f32", "f32_bitwise", "f32_cmp", "f64", "f64_bitwise", "f64_cmp", "fac",
    "float_exprs", "float_literals", "float_memory", "float_misc", "forward", "func", "func_ptrs",
    "globals", "i32", "i64", "if", "int_exprs", "int_literals", "labels", "left-to-right", "load",
    "local_get", "local_set", "local_tee", "loop", "memory", "memory_redundancy", "memory_grow",
    "memory_size", "memory_trap", "names", "nop", "return", "select", "stack", "start", "store",
    "switch", "traps", "type", "typecheck", "unreachable", "unreached-invalid", "unwind",
    "utf8-custom-section-id", "utf8-import-field", "utf8-import-module",
    "e_block", "e_function", "e_globals", "e_locals", "e_memory", "e_module", "e_table",
)

SUPPORT_FILES = (
    "host_functions_tests_0.wasm.hpp",
    "host_functions_tests_1.wasm.hpp",
    "implementation_limits.hpp",
    "reentry.wasm.hpp",
    "host.wasm",
)

EXCEPTION_RENAMES = {
    "guarded_ptr_exception": "exceptions::pointer_out_of_bounds",
    "wasm_bad_alloc": "exceptions::allocation",
    "wasm_exit_exception": "exceptions::exit",
    "wasm_interpreter_exception": "exceptions::interpreter",
    "wasm_memory_exception": "exceptions::memory",
    "wasm_parse_exception": "exceptions::parse",
    "wasm_section_length_exception": "exceptions::section_length",
    "wasm_unsupported_import_exception": "exceptions::unsupported_import",
    "wasm_vector_oob_exception": "exceptions::vector_out_of_bounds",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_head(path: pathlib.Path) -> str:
    import subprocess

    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def active_sources(donor: pathlib.Path) -> list[pathlib.Path]:
    tests = donor / "tests"
    result = [tests / name for name in UNIT_SOURCES]
    result.extend(tests / "spec" / f"{name}_tests.cpp" for name in SPEC_SOURCES)
    missing = [str(path) for path in result if not path.is_file()]
    if missing:
        raise RuntimeError("missing donor tests:\n" + "\n".join(missing))
    return result


def case_manifest(text: str, source: str) -> list[dict[str, object]]:
    pattern = re.compile(
        r'\b(BACKEND_TEST_CASE|TEST_CASE)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)'
    )
    cases = [
        {
            "kind": match.group(1),
            "title": match.group(2),
            "tags": match.group(3),
            "line": text.count("\n", 0, match.start()) + 1,
        }
        for match in pattern.finditer(text)
    ]
    if source == "max_section_elements_tests.cpp":
        for name in ("type", "import", "function", "global", "export", "element", "data"):
            cases.append(
                {
                    "kind": "BACKEND_TEST_CASE",
                    "title": f"Test max_{name}_section_elements",
                    "tags": "[max_section_elements_test]",
                    "line": None,
                }
            )
    return cases


def assertion_manifest(text: str) -> dict[str, int]:
    return {
        name: len(re.findall(rf"\b{name}\s*\(", text))
        for name in ("CHECK", "CHECK_THROWS_AS", "REQUIRE")
    }


def build_manifest(donor: pathlib.Path, fixtures: pathlib.Path) -> dict[str, object]:
    if git_head(donor) != DONOR_COMMIT:
        raise RuntimeError(f"donor HEAD must be {DONOR_COMMIT}")
    if git_head(fixtures) != FIXTURE_COMMIT:
        raise RuntimeError(f"fixture HEAD must be {FIXTURE_COMMIT}")

    sources = []
    for path in active_sources(donor):
        text = path.read_text()
        sources.append(
            {
                "path": path.relative_to(donor).as_posix(),
                "sha256": sha256(path),
                "cases": case_manifest(text, path.name),
                "assertions": assertion_manifest(text),
            }
        )

    fixture_entries = [
        {
            "path": path.relative_to(fixtures).as_posix(),
            "sha256": sha256(path),
        }
        for path in sorted(fixtures.rglob("*.wasm"))
    ]
    return {
        "donor": {
            "repository": "https://github.com/AntelopeIO/eos-vm.git",
            "commit": DONOR_COMMIT,
        },
        "fixtures": {
            "repository": "https://github.com/EOSIO/eos-vm-test-wasms.git",
            "commit": FIXTURE_COMMIT,
            "files": fixture_entries,
        },
        "backend_matrix": {
            "x86_64": ["interpreter", "jit"],
            "other": ["interpreter"],
        },
        "sources": sources,
    }


def wrap_boolean_assertions(text: str, source: str, target: str) -> str:
    """Map a Catch boolean assertion without enabling Boost's C-string comparison."""
    pattern = re.compile(rf"\b{source}\s*\(")
    output: list[str] = []
    offset = 0

    while match := pattern.search(text, offset):
        opening = text.find("(", match.start())
        depth = 1
        index = opening + 1
        quote: str | None = None
        line_comment = False
        block_comment = False

        while index < len(text) and depth:
            char = text[index]
            following = text[index + 1] if index + 1 < len(text) else ""

            if line_comment:
                line_comment = char != "\n"
            elif block_comment:
                if char == "*" and following == "/":
                    block_comment = False
                    index += 1
            elif quote:
                if char == "\\":
                    index += 1
                elif char == quote:
                    quote = None
            elif char == "/" and following == "/":
                line_comment = True
                index += 1
            elif char == "/" and following == "*":
                block_comment = True
                index += 1
            elif char in {'"', "'"}:
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            index += 1

        if depth:
            raise RuntimeError(f"unterminated {source} assertion")

        expression = text[opening + 1 : index - 1]
        output.append(text[offset : match.start()])
        output.append(f"{target}(static_cast<bool>({expression}))")
        offset = index

    output.append(text[offset:])
    return "".join(output)


def replace_live_symbols(text: str) -> str:
    text = re.sub(r"^\s*#\s*include\s*[<\"]catch2/catch\.hpp[>\"]\s*$", "", text, flags=re.M)
    text = re.sub(r"^\s*#\s*include\s*[<\"]eosio/vm/[^>\"]+[>\"]\s*$", "", text, flags=re.M)
    text = re.sub(r"^\s*#\s*include\s*[<\"](?:utils|wasm_config)\.hpp[>\"]\s*$", "", text, flags=re.M)
    text = text.replace("using namespace eosio::vm;", "using namespace forge::vm::wasm;")
    text = text.replace("using namespace eosio;", "")
    text = text.replace("eosio::vm::", "forge::vm::wasm::")
    text = text.replace("eosio_options", "compatibility_options")
    text = text.replace("eosio_max_nested_structures", "max_control_depth")
    text = text.replace("EOS_VM_FROM_WASM", "FORGE_VM_WASM_FROM_WASM")
    text = text.replace("EOS_VM_PRECONDITION", "FORGE_VM_WASM_PRECONDITION")
    text = text.replace("EOS_VM_INVOKE_ON_ALL", "FORGE_VM_WASM_INVOKE_ON_ALL")
    text = text.replace("EOS_VM_INVOKE_ONCE", "FORGE_VM_WASM_INVOKE_ONCE")
    text = text.replace("EOS_VM_INVOKE_ON", "FORGE_VM_WASM_INVOKE_ON")
    for old, new in EXCEPTION_RENAMES.items():
        text = re.sub(rf"\b{old}\b", new, text)
    text = re.sub(r"\bCHECK_THROWS_AS\s*\(", "BOOST_CHECK_THROW(", text)
    text = wrap_boolean_assertions(text, "CHECK", "BOOST_TEST")
    text = wrap_boolean_assertions(text, "REQUIRE", "BOOST_REQUIRE")
    return text


def transform_text(text: str, relative: pathlib.Path) -> str:
    text = replace_live_symbols(text)
    internal = relative.name in {"varint_tests.cpp", "signals_tests.cpp"}
    prefix = re.sub(r"[^a-zA-Z0-9_]", "_", relative.with_suffix("").as_posix())
    if internal:
        include_pattern = r"^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"][ \t]*$"
        includes = re.findall(include_pattern, text, flags=re.M)
        text = re.sub(include_pattern, "", text, flags=re.M)
        heading = (
            "module;\n\n"
            f'#include "test_prelude.hpp"\n'
            + "\n".join(includes)
            + "\n\nmodule forge.vm.wasm.backend;\n\n"
            "#define FORGE_VM_WASM_INTERNAL_TESTS\n"
            f'#include "test_support.hpp"\n\n'
            f"#define FORGE_VM_WASM_TEST_FILE {prefix}\n\n"
        )
    else:
        heading = (
            f'#include "test_prelude.hpp"\n'
            "import forge.vm.wasm.backend;\n"
            f'#include "test_support.hpp"\n\n'
            f"#define FORGE_VM_WASM_TEST_FILE {prefix}\n\n"
        )
    transformed = heading + text.lstrip()
    lines = [line.rstrip() for line in transformed.splitlines()]
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines) + "\n"


def transform_source(path: pathlib.Path, donor: pathlib.Path) -> str:
    relative = path.relative_to(donor / "tests")
    return transform_text(path.read_text(), relative)


def write_port(donor: pathlib.Path, output: pathlib.Path) -> None:
    unit = output / "unit"
    spec = output / "spec"
    support = output / "support"
    unit.mkdir(parents=True, exist_ok=True)
    spec.mkdir(parents=True, exist_ok=True)
    support.mkdir(parents=True, exist_ok=True)

    for source in active_sources(donor):
        relative = source.relative_to(donor / "tests")
        destination = spec / source.name if relative.parts[0] == "spec" else unit / source.name
        destination.write_text(transform_source(source, donor))

    for name in SUPPORT_FILES:
        shutil.copyfile(donor / "tests" / name, support / name)

    regular_units = [name for name in UNIT_SOURCES if name not in {"varint_tests.cpp", "signals_tests.cpp"}]
    source_lines = ["# Generated by port_donor_tests.py. Do not edit.", ""]
    source_lines.append("set(FORGE_VM_WASM_DONOR_UNIT_SOURCES")
    source_lines.extend(f"   ${{CMAKE_CURRENT_LIST_DIR}}/unit/{name}" for name in regular_units)
    source_lines.append(")")
    source_lines.append("")
    source_lines.append("set(FORGE_VM_WASM_DONOR_INTERNAL_SOURCES")
    source_lines.extend(
        f"   ${{CMAKE_CURRENT_LIST_DIR}}/unit/{name}" for name in ("varint_tests.cpp", "signals_tests.cpp")
    )
    source_lines.append(")")
    source_lines.append("")
    source_lines.append("set(FORGE_VM_WASM_DONOR_SPEC_SOURCES")
    source_lines.extend(f"   ${{CMAKE_CURRENT_LIST_DIR}}/spec/{name}_tests.cpp" for name in SPEC_SOURCES)
    source_lines.append(")")
    source_lines.append("")
    (output / "sources.cmake").write_text("\n".join(source_lines))


def compare_tree(expected: pathlib.Path, actual: pathlib.Path) -> list[str]:
    expected_files = {
        path.relative_to(expected): path for path in expected.rglob("*") if path.is_file()
    }
    actual_files = {
        path.relative_to(actual): path
        for path in actual.rglob("*")
        if path.is_file() and path.parts[-1] not in {"donor_manifest.json"}
    }
    differences = []
    for relative in sorted(expected_files.keys() | actual_files.keys()):
        if relative not in expected_files:
            differences.append(f"unexpected {relative}")
        elif relative not in actual_files:
            differences.append(f"missing {relative}")
        elif expected_files[relative].read_bytes() != actual_files[relative].read_bytes():
            differences.append(f"changed {relative}")
    return differences


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--donor", type=pathlib.Path, required=True)
    parser.add_argument("--fixtures", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path(__file__).parent / "donor")
    parser.add_argument("--manifest", type=pathlib.Path, default=pathlib.Path(__file__).parent / "donor_manifest.json")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    if arguments.write == arguments.check:
        parser.error("choose exactly one of --write or --check")

    manifest = build_manifest(arguments.donor, arguments.fixtures)
    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"

    if arguments.write:
        if arguments.output.exists():
            shutil.rmtree(arguments.output)
        write_port(arguments.donor, arguments.output)
        arguments.manifest.write_text(manifest_text)
        return 0

    if not arguments.manifest.is_file() or arguments.manifest.read_text() != manifest_text:
        print("donor manifest differs from the pinned source", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as temporary:
        generated = pathlib.Path(temporary) / "donor"
        write_port(arguments.donor, generated)
        differences = compare_tree(generated, arguments.output)
    if differences:
        print("donor test port differs:", file=sys.stderr)
        print("\n".join(differences), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

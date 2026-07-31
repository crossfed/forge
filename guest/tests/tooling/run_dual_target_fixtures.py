#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run(*command: str, cwd: Path | None = None) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed:\n{' '.join(command)}\n{result.stdout}"
        )
    return result.stdout


def run_failure(*command: str, contains: str, cwd: Path | None = None) -> None:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode == 0:
        raise RuntimeError(
            f"command unexpectedly succeeded:\n{' '.join(command)}"
        )
    if contains not in result.stdout:
        raise RuntimeError(
            f"command did not report {contains!r}:\n"
            f"{' '.join(command)}\n{result.stdout}"
        )


def configure(
    *,
    cmake: str,
    source: Path,
    build: Path,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    guest: bool,
) -> None:
    command = [
        cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_NO_SYSTEM_FROM_IMPORTED=ON",
        f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
        f"-DForgeContract_DIR={contract_package}",
    ]
    if guest:
        command.append(
            f"-DCMAKE_TOOLCHAIN_FILE={contract_package / 'ForgeContractToolchain.cmake'}"
        )
    else:
        command.append(f"-DForge_DIR={forge_package}")
        if sys.platform == "darwin":
            sdk = run("xcrun", "--sdk", "macosx", "--show-sdk-path").strip()
            command.append(f"-DCMAKE_OSX_SYSROOT={sdk}")
    run(*command)


def build(cmake: str, directory: Path, *targets: str) -> None:
    run(
        cmake,
        "--build",
        str(directory),
        "--config",
        "Debug",
        "--target",
        *targets,
        "-j",
        "4",
    )


def artifact_set(directory: Path) -> dict[str, bytes]:
    result = {}
    for suffix in ("wasm", "abi", "contract.json"):
        path = directory / f"product.{suffix}"
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing contract artifact: {path}")
        result[suffix] = path.read_bytes()
    return result


def verify_abi(data: bytes) -> None:
    abi = json.loads(data)
    action = next(
        item for item in abi["actions"] if item["name"] == "beginrev"
    )
    if action["type"] != "begin_revision":
        raise RuntimeError("named action did not use its payload type directly")
    record = next(
        item for item in abi["structs"] if item["name"] == "begin_revision"
    )
    if record["fields"] != [
        {"name": "workspace", "type": "workspace_id"},
        {"name": "inode", "type": "inode_id"},
        {"name": "size", "type": "uint64"},
    ]:
        raise RuntimeError("named action ABI fields are not direct")


def verify_manifest(data: bytes) -> None:
    manifest = json.loads(data)
    if manifest["schema_version"] != 3:
        raise RuntimeError("contract runtime manifest is not schema 3")
    if "source_graph" in manifest:
        raise RuntimeError("runtime manifest contains removed source attestation")
    if len(manifest["wasm"]["sha256"]) != 64:
        raise RuntimeError("runtime manifest has no WASM digest")
    if len(manifest["abi"]["sha256"]) != 64:
        raise RuntimeError("runtime manifest has no ABI digest")


def validate_multi_config(
    *,
    cmake: str,
    source: Path,
    output: Path,
    contract_package: Path,
) -> None:
    build_directory = output / "multi-config"
    run(
        cmake,
        "-S",
        str(source / "multi_config"),
        "-B",
        str(build_directory),
        "-G",
        "Ninja Multi-Config",
        f"-DForgeContract_DIR={contract_package}",
    )
    run(
        cmake,
        "--build",
        str(build_directory),
        "--config",
        "Release",
        "--target",
        "configuration_guest",
        "-j",
        "4",
    )
    artifacts = build_directory / "configuration.guest" / "artifacts"
    if not (artifacts / "built-Release.txt").is_file():
        raise RuntimeError("launcher did not forward the Release configuration")
    if (artifacts / "built-Debug.txt").exists():
        raise RuntimeError("launcher built an unexpected Debug guest configuration")


def write_negative_project(
    root: Path,
    *,
    cmake_body: str,
    modules: dict[str, str],
    contract: str | None = None,
) -> None:
    root.mkdir(parents=True)
    (root / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(ForgeContractNegative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(ForgeContract CONFIG REQUIRED)
"""
        + cmake_body,
        encoding="utf-8",
    )
    for relative, source in modules.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
    if contract is not None:
        (root / "contract.cpp").write_text(contract, encoding="utf-8")


def validate_negative_projects(
    *,
    cmake: str,
    contract_package: Path,
    output: Path,
) -> None:
    source_root = output / "negative-source"
    build_root = output / "negative-build"

    duplicate = source_root / "duplicate-id"
    write_negative_project(
        duplicate,
        cmake_body="""
forge_add_contract_library(
   negative_first ID negative.duplicate
   MODULE_BASE_DIRS first
   MODULE_SOURCES first/value.cppm
)
forge_add_contract_library(
   negative_second ID negative.duplicate
   MODULE_BASE_DIRS second
   MODULE_SOURCES second/value.cppm
)
""",
        modules={
            "first/value.cppm": "export module negative.first;\n",
            "second/value.cppm": "export module negative.second;\n",
        },
    )

    host_only = source_root / "host-only"
    write_negative_project(
        host_only,
        cmake_body="""
add_library(host_only INTERFACE)
forge_add_contract_library(
   negative_protocol ID negative.host_only
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES host_only
)
""",
        modules={
            "include/protocol.cppm": "export module negative.protocol;\n"
        },
    )

    forward_edge = source_root / "forward-edge"
    write_negative_project(
        forward_edge,
        cmake_body="""
forge_add_contract_library(
   negative_protocol ID negative.forward
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES dependency_declared_later
)
""",
        modules={
            "include/protocol.cppm": "export module negative.protocol;\n"
        },
    )

    private_leak = source_root / "private-leak"
    write_negative_project(
        private_leak,
        cmake_body="""
forge_add_contract_library(
   negative_private ID negative.private
   MODULE_BASE_DIRS private
   MODULE_SOURCES private/detail.cppm
)
forge_add_contract_library(
   negative_protocol ID negative.protocol
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/protocol.cppm
   PRIVATE_LIBRARIES negative_private
)
forge_add_contract(
   negative
   SOURCES contract.cpp
   LIBRARIES negative_protocol
)
""",
        modules={
            "private/detail.cppm": (
                "export module negative.detail;\n"
                "export inline constexpr int private_value = 42;\n"
            ),
            "include/protocol.cppm": (
                "export module negative.protocol;\n"
                "export inline constexpr int protocol_value = 42;\n"
            ),
        },
        contract="""import forge.contract;
import negative.detail;
import negative.protocol;

class [[forge::contract("negative")]] entry final
   : public forge::contract::context {
public:
   using forge::contract::context::context;

   [[forge::action]]
   void run() {
      forge::contract::check(
         private_value == protocol_value,
         "unexpected value"
      );
   }
};
""",
    )

    toolchain = contract_package / "ForgeContractToolchain.cmake"
    cases = (
        (duplicate, "duplicate Forge Contract owner ID"),
        (host_only, "contract dependency is not guest-compatible"),
        (forward_edge, "unknown Contract SDK dependency target"),
    )
    for source, expected in cases:
        run_failure(
            cmake,
            "-S",
            str(source),
            "-B",
            str(build_root / source.name),
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DForgeContract_DIR={contract_package}",
            contains=expected,
        )

    private_build = build_root / private_leak.name
    run(
        cmake,
        "-S",
        str(private_leak),
        "-B",
        str(private_build),
        "-G",
        "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DForgeContract_DIR={contract_package}",
    )
    run_failure(
        cmake,
        "--build",
        str(private_build),
        "--target",
        "negative_artifacts",
        "-j",
        "4",
        contains="imports a module through an undeclared library",
    )


def validate(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    source: Path,
    output: Path,
) -> None:
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)

    producer = source / "producer"
    host_build = output / "host"
    direct_build = output / "direct-guest"

    configure(
        cmake=cmake,
        source=producer,
        build=host_build,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        guest=False,
    )
    build(
        cmake,
        host_build,
        "product_protocol_host_tests",
        "product_protocol_vm_tests",
    )
    run(str(host_build / "product_protocol_host_tests"))
    run(str(host_build / "product_protocol_vm_tests"))

    configure(
        cmake=cmake,
        source=producer / "guest",
        build=direct_build,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        guest=True,
    )
    build(cmake, direct_build, "product_artifacts")

    helper = artifact_set(host_build / "product.guest" / "artifacts")
    direct = artifact_set(direct_build / "artifacts")
    for name in helper:
        if helper[name] != direct[name]:
            raise RuntimeError(
                f"direct and launcher contract artifacts differ: {name}"
            )
    verify_abi(direct["abi"])
    verify_manifest(direct["contract.json"])
    validate_multi_config(
        cmake=cmake,
        source=source,
        output=output,
        contract_package=contract_package,
    )
    validate_negative_projects(
        cmake=cmake,
        contract_package=contract_package,
        output=output,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--cxx-compiler", required=True, type=Path)
    parser.add_argument("--forge-package", required=True, type=Path)
    parser.add_argument("--contract-package", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    validate(
        cmake=args.cmake,
        cxx_compiler=args.cxx_compiler,
        forge_package=args.forge_package,
        contract_package=args.contract_package,
        source=args.source.resolve(),
        output=args.output.resolve(),
    )


if __name__ == "__main__":
    main()

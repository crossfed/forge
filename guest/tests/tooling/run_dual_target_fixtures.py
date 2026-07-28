#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run(*command: str, succeeds: bool = True, contains: str | None = None) -> str:
    result = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if succeeds and result.returncode != 0:
        raise RuntimeError(f"command failed:\n{' '.join(command)}\n{result.stdout}")
    if not succeeds and result.returncode == 0:
        raise RuntimeError(f"command unexpectedly succeeded:\n{' '.join(command)}")
    if contains and contains not in result.stdout:
        raise RuntimeError(f"expected diagnostic {contains!r} was not emitted:\n{result.stdout}")
    return result.stdout


def macos_sysroot() -> Path | None:
    if sys.platform != "darwin":
        return None
    result = subprocess.run(
        ("xcrun", "--sdk", "macosx", "--show-sdk-path"),
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return Path(result.stdout.strip()).resolve()


def configure(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    source: Path,
    build: Path,
    product_package: Path | None = None,
    definitions: tuple[str, ...] = (),
    succeeds: bool = True,
    contains: str | None = None,
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
        f"-DForge_DIR={forge_package}",
        f"-DForgeContract_DIR={contract_package}",
    ]
    if (sysroot := macos_sysroot()) is not None:
        command.append(f"-DCMAKE_OSX_SYSROOT={sysroot}")
    if product_package is not None:
        command.append(f"-DProductProtocol_DIR={product_package}")
    command.extend(definitions)
    run(*command, succeeds=succeeds, contains=contains)


def build(
    cmake: str,
    directory: Path,
    *targets: str,
    succeeds: bool = True,
    contains: str | None = None,
) -> None:
    command = [cmake, "--build", str(directory), "-j", "4"]
    if targets:
        command.extend(["--target", *targets])
    run(*command, succeeds=succeeds, contains=contains)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def verify_artifacts(directory: Path, contract: str) -> tuple[dict, dict]:
    for suffix in ("wasm", "abi", "contract.json"):
        artifact = directory / f"{contract}.{suffix}"
        if not artifact.is_file() or artifact.stat().st_size == 0:
            raise RuntimeError(f"missing dual-target contract artifact: {artifact}")
    return read_json(directory / f"{contract}.abi"), read_json(directory / f"{contract}.contract.json")


def verify_direct_action(abi: dict) -> None:
    action = next(entry for entry in abi["actions"] if entry["name"] == "beginrev")
    if action["type"] != "begin_revision":
        raise RuntimeError(f"named action uses a wrapper ABI type: {action['type']}")
    structure = next(entry for entry in abi["structs"] if entry["name"] == "begin_revision")
    if [field["name"] for field in structure["fields"]] != ["workspace", "inode", "size"]:
        raise RuntimeError("named action ABI does not expose payload fields directly")
    if any(entry["name"] == "beginrev" for entry in abi["structs"]):
        raise RuntimeError("named action ABI contains a synthetic handler wrapper")


def verify_source_graph(manifest: dict) -> None:
    if manifest["schema_version"] != 2:
        raise RuntimeError("dual-target contract manifest does not use schema v2")
    graph = manifest["source_graph"]
    expected_roles = {
        "product.chain.values": {"module", "public_header"},
        "product.chain.limits": {"module", "implementation"},
        "product.chain.protocol": {"module", "implementation", "private_header"},
    }
    for owner, expected in expected_roles.items():
        observed = {entry["role"] for entry in graph["files"] if entry["owner"] == owner}
        if observed != expected:
            raise RuntimeError(f"source graph roles for {owner} are {sorted(observed)}, expected {sorted(expected)}")

    observed_edges = {
        (entry["owner"], entry["kind"], entry["dependency"], entry["scope"])
        for entry in graph["dependencies"]
    }
    expected_edges = {
        ("contract:product", "library", "product.chain.protocol", "public"),
        ("product.chain.protocol", "library", "product.chain.values", "public"),
        ("product.chain.protocol", "library", "product.chain.limits", "private"),
        ("product.chain.limits", "component", "forge.crypto.digest", "private"),
    }
    if not expected_edges <= observed_edges:
        raise RuntimeError(f"source graph omits dependency edges: {sorted(expected_edges - observed_edges)}")
    if len(graph["sha256"]) != 64:
        raise RuntimeError("source graph has no canonical SHA-256")
    if graph["files"] != sorted(
        graph["files"],
        key=lambda entry: (entry["owner"], entry["role"], entry["logical_path"], entry["sha256"]),
    ):
        raise RuntimeError("source graph files are not canonicalized")
    if graph["dependencies"] != sorted(
        graph["dependencies"],
        key=lambda entry: (entry["owner"], entry["kind"], entry["dependency"], entry["scope"]),
    ):
        raise RuntimeError("source graph dependencies are not canonicalized")


def verify_compilation_metadata(build_directory: Path) -> None:
    object_lists = sorted((build_directory / "product.contract" / "contract-compilations").glob("library-*.objects"))
    if len(object_lists) != 3:
        raise RuntimeError(f"expected three contract-library compilations, found {len(object_lists)}")
    metadata_count = 0
    for object_list in object_lists:
        for object_name in object_list.read_text(encoding="utf-8").splitlines():
            if not object_name:
                continue
            metadata_path = Path(object_name + ".forge-contract-metadata.json")
            metadata = read_json(metadata_path)
            if metadata.get("version") != 1:
                raise RuntimeError(f"invalid compilation metadata schema: {metadata_path}")
            for field in ("imports", "exports", "provides"):
                values = metadata.get(field)
                if not isinstance(values, list) or values != sorted(set(values)):
                    raise RuntimeError(f"compilation metadata has non-canonical {field}: {metadata_path}")
            dependencies = metadata.get("dependencies")
            if not isinstance(dependencies, list):
                raise RuntimeError(f"compilation metadata has no dependencies: {metadata_path}")
            for dependency in dependencies:
                if (
                    not isinstance(dependency, dict)
                    or not isinstance(dependency.get("path"), str)
                    or not isinstance(dependency.get("system"), bool)
                ):
                    raise RuntimeError(f"invalid compilation dependency: {metadata_path}")
            metadata_count += 1
    if metadata_count != 5:
        raise RuntimeError(f"expected five contract-library translation units, found {metadata_count}")


def verify_relocatable_package(prefix: Path, forbidden: list[Path]) -> None:
    compiled_modules = [
        path for path in prefix.rglob("*") if path.is_file() and path.suffix.lower() in {".pcm", ".bmi"}
    ]
    if compiled_modules:
        raise RuntimeError(f"protocol package transports a compiled module: {compiled_modules[0]}")

    needles = [str(path.resolve()) for path in forbidden]
    for path in prefix.rglob("*"):
        if not path.is_file() or path.suffix not in {".cmake", ".json", ".pc", ".txt"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for needle in needles:
            if needle in text:
                raise RuntimeError(f"protocol package retains an absolute path: {path}: {needle}")


def write_project(root: Path, body: str) -> Path:
    (root / "include").mkdir(parents=True)
    (root / "src").mkdir()
    (root / "include" / "protocol.cppm").write_text(
        "export module negative.protocol;\nexport inline constexpr auto protocol_value = 42;\n",
        encoding="utf-8",
    )
    (root / "src" / "protocol.cpp").write_text(
        "module negative.protocol;\n",
        encoding="utf-8",
    )
    (root / "contract.cpp").write_text(
        """import forge.contract;
import negative.protocol;

class [[forge::contract("negative")]] negative_contract : public forge::contract::context {
 public:
   using context::context;
   [[forge::action]] void verify() {
      forge::contract::check(protocol_value == 42, "invalid value");
   }
};
""",
        encoding="utf-8",
    )
    (root / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(NegativeContractGraph LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(Forge CONFIG REQUIRED COMPONENTS raw crypto_digest)
find_package(ForgeContract CONFIG REQUIRED)
"""
        + body,
        encoding="utf-8",
    )
    return root


def check_configure_failures(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    output: Path,
) -> None:
    fixtures = output / "configure"
    outside = fixtures / "outside.cppm"
    outside.parent.mkdir(parents=True)
    outside.write_text("export module negative.outside;\n", encoding="utf-8")

    cases = [
        (
            "outside-root",
            """forge_add_contract_library(
   protocol ID negative.outside SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES ../outside.cppm
)
""",
            "outside SOURCE_ROOT",
        ),
        (
            "duplicate-logical-path",
            """forge_add_contract_library(
   protocol ID negative.duplicate.path SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_HEADERS include/protocol.cppm
)
""",
            "declared more than once",
        ),
        (
            "host-only-dependency",
            """add_library(host_only INTERFACE)
forge_add_contract_library(
   protocol ID negative.host.only SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES host_only
)
""",
            "not guest-compatible",
        ),
        (
            "duplicate-id",
            """forge_add_contract_library(
   first ID negative.duplicate.id SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
file(COPY include/protocol.cppm DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/second")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/second/protocol.cppm" "export module negative.second;\\n")
forge_add_contract_library(
   second ID negative.duplicate.id SOURCE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/second"
   MODULE_BASE_DIRS . MODULE_SOURCES protocol.cppm
)
""",
            "duplicate Forge Contract library ID",
        ),
        (
            "immutable-alias",
            """forge_add_contract_library(
   protocol ID negative.immutable SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
target_sources(protocol PRIVATE src/protocol.cpp)
""",
            "ALIAS target",
        ),
    ]
    for name, body, diagnostic in cases:
        source = write_project(fixtures / name, body)
        configure(
            cmake=cmake,
            cxx_compiler=cxx_compiler,
            forge_package=forge_package,
            contract_package=contract_package,
            source=source,
            build=fixtures / f"{name}-build",
            succeeds=False,
            contains=diagnostic,
        )


def check_build_failures(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    output: Path,
) -> None:
    fixtures = output / "build"

    undeclared = write_project(
        fixtures / "undeclared-header",
        """forge_add_contract_library(
   protocol ID negative.undeclared SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
forge_add_contract(negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES contract.cpp LIBRARIES protocol)
""",
    )
    (undeclared / "hidden.hpp").write_text("#pragma once\ninline constexpr auto hidden_value = 42;\n", encoding="utf-8")
    (undeclared / "contract.cpp").write_text(
        """#include "hidden.hpp"
import forge.contract;
import negative.protocol;

class [[forge::contract("negative")]] negative_contract : public forge::contract::context {
 public:
   using context::context;
   [[forge::action]] void verify() {
      forge::contract::check(hidden_value == protocol_value, "invalid value");
   }
};
""",
        encoding="utf-8",
    )

    private_import = write_project(
        fixtures / "private-import",
        """forge_add_contract_library(
   private_library ID negative.private SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/private"
   MODULE_BASE_DIRS . MODULE_SOURCES detail.cppm
)
forge_add_contract_library(
   protocol ID negative.protocol SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PRIVATE_LIBRARIES private_library
)
forge_add_contract(negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES contract.cpp LIBRARIES protocol)
""",
    )
    (private_import / "private").mkdir()
    (private_import / "private" / "detail.cppm").write_text(
        "export module negative.detail;\nexport inline constexpr auto private_value = 42;\n",
        encoding="utf-8",
    )
    (private_import / "contract.cpp").write_text(
        """import forge.contract;
import negative.protocol;
import negative.detail;

class [[forge::contract("negative")]] negative_contract : public forge::contract::context {
 public:
   using context::context;
   [[forge::action]] void verify() {
      forge::contract::check(protocol_value == private_value, "invalid value");
   }
};
""",
        encoding="utf-8",
    )

    private_export = write_project(
        fixtures / "private-export",
        """forge_add_contract_library(
   private_library ID negative.private SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/private"
   MODULE_BASE_DIRS . MODULE_SOURCES detail.cppm
)
forge_add_contract_library(
   protocol ID negative.protocol SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PRIVATE_LIBRARIES private_library
)
forge_add_contract(negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES contract.cpp LIBRARIES protocol)
""",
    )
    (private_export / "private").mkdir()
    (private_export / "private" / "detail.cppm").write_text(
        "export module negative.detail;\nexport inline constexpr auto private_value = 42;\n",
        encoding="utf-8",
    )
    (private_export / "include" / "protocol.cppm").write_text(
        """export module negative.protocol;
export import negative.detail;
export inline constexpr auto protocol_value = private_value;
""",
        encoding="utf-8",
    )

    cases = [
        (undeclared, "contract source dependency is not declared"),
        (private_import, "module 'negative.detail' not found"),
        (private_export, "exports a module through a private dependency"),
    ]
    for source, diagnostic in cases:
        build_directory = fixtures / f"{source.name}-build"
        configure(
            cmake=cmake,
            cxx_compiler=cxx_compiler,
            forge_package=forge_package,
            contract_package=contract_package,
            source=source,
            build=build_directory,
        )
        build(cmake, build_directory, succeeds=False, contains=diagnostic)


def validate(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    source: Path,
    host_source: Path,
    output: Path,
) -> None:
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)

    producer_build = output / "producer-build"
    producer_install = output / "producer-install"
    configure(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source / "producer",
        build=producer_build,
        definitions=(f"-DCMAKE_INSTALL_PREFIX={producer_install}",),
    )
    build(cmake, producer_build)
    run(str(producer_build / "product_protocol_host_tests"))
    abi, manifest = verify_artifacts(producer_build, "product")
    verify_direct_action(abi)
    verify_source_graph(manifest)
    verify_compilation_metadata(producer_build)
    initial_digest = manifest["source_graph"]["sha256"]
    build(cmake, producer_build)
    if verify_artifacts(producer_build, "product")[1]["source_graph"]["sha256"] != initial_digest:
        raise RuntimeError("source graph digest changed across an incremental rebuild")
    run(cmake, "--install", str(producer_build))
    verify_relocatable_package(producer_install, [source, producer_build])

    product_wasm = output / "product.wasm"
    shutil.copy2(producer_build / "product.wasm", product_wasm)
    relocated = output / "product-relocated"
    shutil.copytree(producer_install, relocated)
    shutil.rmtree(producer_install)
    product_package = relocated / "lib" / "cmake" / "ProductProtocol"
    if not any((product_package / "cxx-modules").glob("*.cmake")):
        raise RuntimeError("installed protocol package has no CMake module metadata")

    consumer_build = output / "consumer-build"
    configure(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_package=product_package,
        source=source / "consumer",
        build=consumer_build,
    )
    build(cmake, consumer_build)
    run(str(consumer_build / "product_protocol_consumer"))
    verify_artifacts(consumer_build, "consumer")

    vm_build = output / "vm-build"
    configure(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_package=product_package,
        source=source / "vm",
        build=vm_build,
        definitions=(
            f"-DPRODUCT_PROTOCOL_WASM={product_wasm}",
            f"-DFORGE_CONTRACT_TEST_HOST_SOURCE={host_source}",
        ),
    )
    build(cmake, vm_build)
    run(str(vm_build / "product_protocol_vm_tests"))

    check_configure_failures(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        output=output / "negative",
    )
    check_build_failures(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        output=output / "negative",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--cxx-compiler", required=True, type=Path)
    parser.add_argument("--forge-package", required=True, type=Path)
    parser.add_argument("--contract-package", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--host-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    validate(
        cmake=args.cmake,
        cxx_compiler=args.cxx_compiler.absolute(),
        forge_package=args.forge_package.resolve(),
        contract_package=args.contract_package.resolve(),
        source=args.source.resolve(),
        host_source=args.host_source.resolve(),
        output=args.output.resolve(),
    )


if __name__ == "__main__":
    main()

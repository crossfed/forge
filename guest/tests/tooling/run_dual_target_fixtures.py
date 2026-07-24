#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional


def run(*command: str, succeeds: bool = True, contains: Optional[str] = None) -> str:
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


def macos_sysroot() -> Optional[Path]:
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
    args,
    source: Path,
    build: Path,
    *,
    product_package: Optional[Path] = None,
    definitions=(),
    build_type: str = "Release",
    succeeds: bool = True,
    contains: Optional[str] = None,
) -> None:
    command = [
        args.cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DCMAKE_NO_SYSTEM_FROM_IMPORTED=ON",
        f"-DCMAKE_CXX_COMPILER={args.cxx_compiler}",
        f"-DForge_DIR={args.forge_package}",
        f"-DForgeContract_DIR={args.contract_package}",
    ]
    if args.macos_sysroot:
        command.append(f"-DCMAKE_OSX_SYSROOT={args.macos_sysroot}")
    if product_package:
        command.append(f"-DProductProtocol_DIR={product_package}")
    command.extend(definitions)
    run(*command, succeeds=succeeds, contains=contains)


def build(args, directory: Path, *targets: str) -> None:
    command = [args.cmake, "--build", str(directory), "-j", "4"]
    if targets:
        command.extend(["--target", *targets])
    run(*command)


def verify_artifacts(directory: Path, contract: str) -> tuple[dict, dict]:
    paths = [directory / f"{contract}.{suffix}" for suffix in ("wasm", "abi", "contract.json")]
    for path in paths:
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing dual-target contract artifact: {path}")
    return (
        json.loads((directory / f"{contract}.abi").read_text(encoding="utf-8")),
        json.loads((directory / f"{contract}.contract.json").read_text(encoding="utf-8")),
    )


def verify_direct_action(abi: dict) -> None:
    action = next(entry for entry in abi["actions"] if entry["name"] == "beginrev")
    if action["type"] != "begin_revision":
        raise RuntimeError(f"named action uses a wrapper ABI type: {action['type']}")
    structure = next(entry for entry in abi["structs"] if entry["name"] == "begin_revision")
    if [field["name"] for field in structure["fields"]] != ["workspace", "inode", "size"]:
        raise RuntimeError("named action ABI does not expose the payload fields directly")
    if any(entry["name"] == "beginrev" for entry in abi["structs"]):
        raise RuntimeError("named action ABI contains a synthetic handler wrapper")


def verify_source_graph(manifest: dict) -> None:
    if manifest["schema_version"] != 2:
        raise RuntimeError("dual-target contract manifest does not use schema v2")
    graph = manifest["source_graph"]
    roles = {entry["role"] for entry in graph["files"] if entry["owner"] == "product.chain.protocol"}
    if roles != {"module", "implementation", "public_header", "private_header"}:
        raise RuntimeError(f"dual-target source graph has incomplete roles: {sorted(roles)}")
    expected_edge = {"owner": "contract:product", "dependency": "product.chain.protocol"}
    if expected_edge not in graph["dependencies"]:
        raise RuntimeError("dual-target source graph omits the contract-to-library dependency")
    if any(Path(entry["logical_path"]).is_absolute() for entry in graph["files"]):
        raise RuntimeError("dual-target source graph contains an absolute logical path")


def package_metadata(prefix: Path) -> list[Path]:
    return [
        path
        for path in prefix.rglob("*")
        if path.is_file() and path.suffix in {".cmake", ".json", ".pc", ".txt"}
    ]


def verify_relocatable_package(prefix: Path, forbidden: list[Path]) -> None:
    transported_modules = [
        path for path in prefix.rglob("*") if path.is_file() and path.suffix.lower() in {".pcm", ".bmi"}
    ]
    if transported_modules:
        raise RuntimeError(f"contract protocol package transports a compiled module: {transported_modules[0]}")

    needles = [str(path.resolve()) for path in forbidden]
    for metadata in package_metadata(prefix):
        text = metadata.read_text(encoding="utf-8", errors="ignore")
        for needle in needles:
            if needle in text:
                raise RuntimeError(f"contract protocol package retains an absolute path: {metadata}: {needle}")


def write_negative_project(root: Path, body: str) -> Path:
    root.mkdir(parents=True)
    (root / "include").mkdir()
    (root / "include" / "protocol.cppm").write_text("export module negative.protocol;\n", encoding="utf-8")
    (root / "contract.cpp").write_text("import forge.contract;\n", encoding="utf-8")
    (root / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(NegativeContractLibrary LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(Forge CONFIG REQUIRED COMPONENTS raw)
find_package(ForgeContract CONFIG REQUIRED)
"""
        + body,
        encoding="utf-8",
    )
    return root


def check_negative_projects(args, output: Path) -> None:
    output.mkdir(parents=True)
    outside = output / "outside.cppm"
    outside.write_text("export module negative.outside;\n", encoding="utf-8")

    source_escape = write_negative_project(
        output / "source-escape",
        """forge_add_contract_library(
   protocol ID negative.source.escape SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES ../outside.cppm LIBRARIES Forge::forge_raw
)
""",
    )
    configure(
        args,
        source_escape,
        output / "source-escape-build",
        succeeds=False,
        contains="outside SOURCE_ROOT",
    )

    duplicate = write_negative_project(
        output / "duplicate",
        """forge_add_contract_library(
   protocol ID negative.duplicate SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/protocol.cppm
   PUBLIC_HEADERS include/protocol.cppm
   LIBRARIES Forge::forge_raw
)
""",
    )
    configure(
        args,
        duplicate,
        output / "duplicate-build",
        succeeds=False,
        contains="declared more than once",
    )

    host_only = write_negative_project(
        output / "host-only",
        """add_library(host_only INTERFACE)
forge_add_contract_library(
   protocol ID negative.host.only SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm LIBRARIES host_only
)
""",
    )
    configure(
        args,
        host_only,
        output / "host-only-build",
        succeeds=False,
        contains="not guest-compatible",
    )

    generator_expression = write_negative_project(
        output / "generator-expression",
        """add_library(host_only INTERFACE)
forge_add_contract_library(
   protocol ID negative.generator.expression SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
target_link_libraries(protocol PUBLIC "$<BUILD_INTERFACE:host_only>")
forge_add_contract(expression LIBRARIES protocol SOURCES contract.cpp)
""",
    )
    configure(
        args,
        generator_expression,
        output / "generator-expression-build",
        succeeds=False,
        contains="generator expressions are not supported",
    )

    compile_definition = write_negative_project(
        output / "compile-definition",
        """forge_add_contract_library(
   protocol ID negative.compile.definition SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
target_compile_definitions(protocol PRIVATE PROTOCOL_LAYOUT_VERSION=2)
forge_add_contract(definition LIBRARIES protocol SOURCES contract.cpp)
""",
    )
    configure(
        args,
        compile_definition,
        output / "compile-definition-build",
        succeeds=False,
        contains="COMPILE_DEFINITIONS;",
    )

    compile_option = write_negative_project(
        output / "compile-option",
        """forge_add_contract_library(
   protocol ID negative.compile.option SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
target_compile_options(protocol PRIVATE -fpack-struct=1)
forge_add_contract(option LIBRARIES protocol SOURCES contract.cpp)
""",
    )
    configure(
        args,
        compile_option,
        output / "compile-option-build",
        succeeds=False,
        contains="COMPILE_OPTIONS;",
    )

    include_directory = write_negative_project(
        output / "include-directory",
        """forge_add_contract_library(
   protocol ID negative.include.directory SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
target_include_directories(protocol PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
forge_add_contract(includedirectory LIBRARIES protocol SOURCES contract.cpp)
""",
    )
    configure(
        args,
        include_directory,
        output / "include-directory-build",
        succeeds=False,
        contains="INCLUDE_DIRECTORIES:",
    )

    directory_scope = write_negative_project(
        output / "directory-scope",
        """forge_add_contract_library(
   dependency ID negative.directory.dependency SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
forge_add_contract_library(
   protocol ID negative.directory.protocol SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
set_property(
   TARGET protocol
   PROPERTY INTERFACE_LINK_LIBRARIES "::@(fixture);dependency"
)
forge_add_contract(directoryscope LIBRARIES protocol SOURCES contract.cpp)
""",
    )
    configure(
        args,
        directory_scope,
        output / "directory-scope-build",
        succeeds=False,
        contains="unterminated CMake directory-scope wrapper",
    )

    cycle = write_negative_project(
        output / "cycle",
        """forge_add_contract_library(
   first ID negative.first SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm LIBRARIES Forge::forge_raw
)
file(COPY include/protocol.cppm DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/second")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/second/protocol.cppm" "export module negative.second;\\n")
forge_add_contract_library(
   second ID negative.second SOURCE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/second"
   MODULE_BASE_DIRS . MODULE_SOURCES protocol.cppm LIBRARIES Forge::forge_raw
)
target_link_libraries(first PUBLIC second)
target_link_libraries(second PUBLIC first)
forge_add_contract(cycle LIBRARIES first SOURCES contract.cpp)
""",
    )
    configure(
        args,
        cycle,
        output / "cycle-build",
        succeeds=False,
        contains="cycle in Forge Contract library dependencies",
    )


def check_dependency_normalization(args, output: Path) -> None:
    source = output / "source"
    source.mkdir(parents=True)
    (source / "include").mkdir()
    (source / "libraries").mkdir()
    (source / "src").mkdir()
    (source / "include" / "dependency.cppm").write_text(
        """export module self_contained.dependency;

export int dependency_value();
""",
        encoding="utf-8",
    )
    (source / "src" / "dependency.cpp").write_text(
        """module self_contained.dependency;

int dependency_value() {
   return 20;
}
""",
        encoding="utf-8",
    )
    (source / "include" / "second_dependency.cppm").write_text(
        """export module self_contained.second_dependency;

export int second_dependency_value();
""",
        encoding="utf-8",
    )
    (source / "src" / "second_dependency.cpp").write_text(
        """module self_contained.second_dependency;

int second_dependency_value() {
   return 22;
}
""",
        encoding="utf-8",
    )
    (source / "include" / "protocol.cppm").write_text(
        """export module self_contained.protocol;

export int protocol_value();
""",
        encoding="utf-8",
    )
    (source / "src" / "protocol.cpp").write_text(
        """module self_contained.protocol;

import self_contained.dependency;
import self_contained.second_dependency;

int protocol_value() {
   return dependency_value() + second_dependency_value();
}
""",
        encoding="utf-8",
    )
    (source / "contract.cpp").write_text(
        """import forge.contract;
import self_contained.protocol;

class [[forge::contract("selfcontained")]] self_contained_contract
    : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void ping() {
      forge::contract::check(protocol_value() == 42, "private dependency was not linked");
   }
};
""",
        encoding="utf-8",
    )
    (source / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(SelfContainedContractLibrary LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(ForgeContract CONFIG REQUIRED)
add_subdirectory(libraries)
target_link_libraries(protocol PRIVATE dependency dependency second_dependency)
get_target_property(protocol_dependencies protocol INTERFACE_LINK_LIBRARIES)
if(NOT protocol_dependencies MATCHES "::@\\\\(")
   message(FATAL_ERROR "fixture did not produce a CMake directory-scope wrapper")
endif()
forge_add_contract(
   selfcontained LIBRARIES protocol protocol SOURCES contract.cpp
)
""",
        encoding="utf-8",
    )
    (source / "libraries" / "CMakeLists.txt").write_text(
        """forge_add_contract_library(
   dependency ID self_contained.dependency SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.."
   MODULE_BASE_DIRS include MODULE_SOURCES include/dependency.cppm
   SOURCES src/dependency.cpp
)
forge_add_contract_library(
   second_dependency ID self_contained.second_dependency SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.."
   MODULE_BASE_DIRS include MODULE_SOURCES include/second_dependency.cppm
   SOURCES src/second_dependency.cpp
)
forge_add_contract_library(
   protocol ID self_contained.protocol SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.."
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   SOURCES src/protocol.cpp
)
""",
        encoding="utf-8",
    )

    build_directory = output / "build"
    configure(args, source, build_directory)
    build(args, build_directory)
    _, manifest = verify_artifacts(build_directory, "selfcontained")
    edges = [
        edge
        for edge in manifest["source_graph"]["dependencies"]
        if edge
        == {
            "owner": "contract:selfcontained",
            "dependency": "self_contained.protocol",
        }
    ]
    if len(edges) != 1:
        raise RuntimeError(f"root contract library dependency was not normalized: {edges}")
    private_edges = sorted(
        (edge["owner"], edge["dependency"])
        for edge in manifest["source_graph"]["dependencies"]
        if edge["owner"] == "self_contained.protocol"
    )
    expected_private_edges = [
        ("self_contained.protocol", "self_contained.dependency"),
        ("self_contained.protocol", "self_contained.second_dependency"),
    ]
    if private_edges != expected_private_edges:
        raise RuntimeError(
            f"multi-entry LINK_ONLY contract dependencies were not attested: {private_edges}"
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
    args.macos_sysroot = macos_sysroot()

    output = args.output.resolve()
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)

    producer_build = output / "producer-build"
    producer_install = output / "producer-install"
    configure(args, args.source / "producer", producer_build)
    build(args, producer_build)
    run(
        args.cmake,
        "--install",
        str(producer_build),
        "--prefix",
        str(producer_install),
    )
    run(str(producer_build / "product_protocol_host_tests"))

    producer_abi, producer_manifest = verify_artifacts(producer_build, "product")
    verify_direct_action(producer_abi)
    verify_source_graph(producer_manifest)
    verify_relocatable_package(producer_install, [args.source, producer_build])

    relocated = output / "product-protocol-relocated"
    shutil.copytree(producer_install, relocated)
    product_package = relocated / "lib" / "cmake" / "ProductProtocol"
    module_metadata = product_package / "cxx-modules"
    if not module_metadata.is_dir() or not any(module_metadata.glob("*.cmake")):
        raise RuntimeError("installed protocol package omits CMake C++ module metadata")
    consumer_build = output / "consumer-build"
    configure(
        args,
        args.source / "consumer",
        consumer_build,
        product_package=product_package,
    )
    build(args, consumer_build)
    run(str(consumer_build / "product_protocol_consumer"))
    verify_artifacts(consumer_build, "consumer")

    vm_build = output / "vm-build"
    configure(
        args,
        args.source / "vm",
        vm_build,
        product_package=product_package,
        build_type="Debug",
        definitions=(
            f"-DPRODUCT_PROTOCOL_WASM={producer_build / 'product.wasm'}",
            f"-DFORGE_CONTRACT_TEST_HOST_SOURCE={args.source.parent / 'host'}",
        ),
    )
    build(args, vm_build)
    run(str(vm_build / "product_protocol_vm_tests"))

    check_negative_projects(args, output / "negative")
    check_dependency_normalization(args, output / "dependency-normalization")


if __name__ == "__main__":
    main()

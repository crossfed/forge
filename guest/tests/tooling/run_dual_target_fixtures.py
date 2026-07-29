#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(
    *command: str,
    succeeds: bool = True,
    contains: str | None = None,
    environment: dict[str, str] | None = None,
) -> str:
    result = subprocess.run(
        command,
        check=False,
        env=environment,
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
    environment: dict[str, str] | None = None,
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
    run(*command, succeeds=succeeds, contains=contains, environment=environment)


def build(
    cmake: str,
    directory: Path,
    *targets: str,
    succeeds: bool = True,
    contains: str | None = None,
    environment: dict[str, str] | None = None,
) -> None:
    command = [cmake, "--build", str(directory), "-j", "4"]
    if targets:
        command.extend(["--target", *targets])
    run(*command, succeeds=succeeds, contains=contains, environment=environment)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json_newer_than(path: Path, value: dict, previous_mtime_ns: int) -> None:
    path.write_text(json.dumps(value, separators=(",", ":")) + "\n", encoding="utf-8")
    current = path.stat()
    if current.st_mtime_ns <= previous_mtime_ns:
        os.utime(path, ns=(current.st_atime_ns, previous_mtime_ns + 1))


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


def append_length(output: bytearray, value: int) -> None:
    output.extend(value.to_bytes(8, byteorder="big"))


def append_field(output: bytearray, value: str) -> None:
    encoded = value.encode("utf-8")
    append_length(output, len(encoded))
    output.extend(encoded)


def verify_source_graph(manifest: dict, descriptor: dict) -> None:
    if manifest["schema_version"] != 2:
        raise RuntimeError("dual-target contract manifest does not use schema v2")
    graph = manifest["source_graph"]
    if graph["root_owner"] != descriptor["root"]["owner"]:
        raise RuntimeError("source graph has the wrong root owner")
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
        ("product.chain.limits", "component", "forge.crypto.digest", "public"),
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
    expected_components = sorted(
        (
            {"id": component["id"], "modules": sorted(component["modules"])}
            for component in descriptor["components"]
        ),
        key=lambda component: (component["id"], component["modules"]),
    )
    if graph["components"] != expected_components:
        raise RuntimeError("source graph component module ownership does not match the descriptor")

    encoded = bytearray()
    append_field(encoded, "forge.contract.source-graph.v2")
    append_field(encoded, "root")
    append_field(encoded, graph["root_owner"])
    append_length(encoded, len(graph["files"]))
    for file in graph["files"]:
        append_field(encoded, "file")
        append_field(encoded, file["owner"])
        append_field(encoded, file["role"])
        append_field(encoded, file["logical_path"])
        append_field(encoded, file["sha256"])
    append_length(encoded, len(graph["dependencies"]))
    for edge in graph["dependencies"]:
        append_field(encoded, "dependency")
        append_field(encoded, edge["owner"])
        append_field(encoded, edge["kind"])
        append_field(encoded, edge["dependency"])
        append_field(encoded, edge["scope"])
    append_length(encoded, len(graph["components"]))
    for component in graph["components"]:
        append_field(encoded, "component")
        append_field(encoded, component["id"])
        append_length(encoded, len(component["modules"]))
        for module in component["modules"]:
            append_field(encoded, module)
    if graph["sha256"] != hashlib.sha256(encoded).hexdigest():
        raise RuntimeError("source graph digest does not cover its canonical semantic fields")


def verify_contract_graph(build_directory: Path) -> tuple[dict[str, str], dict[Path, str]]:
    graph = read_json(build_directory / "product.contract-graph.json")
    root_components = graph["root"]["components"]
    if root_components != ["forge.contract.runtime"]:
        raise RuntimeError(f"contract graph has unexpected foundation components: {root_components}")

    components = {entry["id"]: entry for entry in graph["components"]}
    runtime = components.get("forge.contract.runtime")
    if runtime is None:
        raise RuntimeError("contract graph omits the Forge Contract runtime component")
    if runtime["dependencies"] != [
        "forge.raw",
        "forge.codec.base64",
        "forge.codec.base58",
        "forge.codec.hex",
        "forge.chain.protocol",
    ]:
        raise RuntimeError(f"contract runtime dependency graph is incomplete: {runtime['dependencies']}")

    module_owners: dict[str, str] = {}
    for component_id, component in components.items():
        for module in component["modules"]:
            if module in module_owners:
                raise RuntimeError(f"contract graph gives module {module} multiple owners")
            module_owners[module] = component_id
    for required in ("forge.contract", "forge.contract.multi_index", "forge.crypto.digest.sha256"):
        if required not in module_owners:
            raise RuntimeError(f"contract graph does not own exposed SDK module {required}")

    source_owners: dict[Path, str] = {}
    for library in graph["libraries"]:
        for file in library["files"]:
            if file["role"] not in ("module", "implementation"):
                continue
            source = Path(file["physical_path"]).resolve()
            if source in source_owners:
                raise RuntimeError(f"contract graph gives source {source} multiple owners")
            source_owners[source] = library["id"]
    return module_owners, source_owners


def component_metadata_target(path: Path) -> str:
    target_parts = [
        part.removesuffix(".dir").split("@", 1)[0]
        for part in path.parts
        if part.startswith("forge_contract_component_")
    ]
    if len(target_parts) != 1:
        raise RuntimeError(f"cannot identify guest component target for module metadata: {path}")
    return target_parts[0]


def component_metadata_files(build_directory: Path) -> list[Path]:
    component_root = build_directory / "product.contract" / "CMakeFiles"
    dependency_files = sorted(component_root.glob("forge_contract_component_*.dir/**/*.ddi"))
    if not dependency_files:
        raise RuntimeError("guest component compilation produced no module dependency metadata")
    return dependency_files


def verify_component_module_metadata(build_directory: Path, module_owners: dict[str, str]) -> None:
    component_owners = {
        hashlib.sha256(component_id.encode()).hexdigest()[:16]: component_id
        for component_id in set(module_owners.values())
    }
    provided_modules: dict[str, str] = {}
    for path in component_metadata_files(build_directory):
        target_key = component_metadata_target(path).removeprefix("forge_contract_component_")
        component_owner = component_owners.get(target_key)
        if component_owner is None:
            raise RuntimeError(f"module metadata belongs to an unknown guest component target: {path}")

        metadata = read_json(path)
        for rule in metadata.get("rules", []):
            for provided in rule.get("provides", []):
                name = provided.get("logical-name")
                if not isinstance(name, str) or not name:
                    raise RuntimeError(f"invalid compiler-provided module name: {path}")
                expected_owner = module_owners.get(name)
                if expected_owner is None:
                    raise RuntimeError(f"compiler reports an undescribed guest component module: {name}")
                if expected_owner != component_owner:
                    raise RuntimeError(
                        "compiler reports a guest component module under the wrong owner: "
                        f"module={name}, expected={expected_owner}, actual={component_owner}"
                    )
                previous_owner = provided_modules.setdefault(name, component_owner)
                if previous_owner != component_owner:
                    raise RuntimeError(f"compiler reports multiple owners for guest component module: {name}")

    described_modules = set(module_owners)
    compiled_modules = set(provided_modules)
    if compiled_modules != described_modules:
        missing = sorted(described_modules - compiled_modules)
        unexpected = sorted(compiled_modules - described_modules)
        raise RuntimeError(
            "guest component descriptor differs from compiler module metadata: "
            f"missing={missing}, unexpected={unexpected}"
        )


def verify_compilation_metadata(
    build_directory: Path,
    module_owners: dict[str, str],
    source_owners: dict[Path, str],
) -> None:
    object_lists = sorted((build_directory / "product.contract" / "contract-compilations").glob("library-*.objects"))
    if len(object_lists) != 3:
        raise RuntimeError(f"expected three contract-library compilations, found {len(object_lists)}")
    metadata_records: list[tuple[Path, str, dict]] = []
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
            source = Path(metadata["source"]).resolve()
            owner = source_owners.get(source)
            if owner is None:
                raise RuntimeError(f"compilation metadata source has no descriptor owner: {source}")
            for module in metadata["provides"]:
                existing = module_owners.setdefault(module, owner)
                if existing != owner:
                    raise RuntimeError(f"contract module {module} has multiple owners")
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
            metadata_records.append((metadata_path, owner, metadata))
    if len(metadata_records) != 5:
        raise RuntimeError(f"expected five contract-library translation units, found {len(metadata_records)}")
    for metadata_path, _, metadata in metadata_records:
        for module in (*metadata["imports"], *metadata["exports"]):
            if module not in module_owners:
                raise RuntimeError(f"compilation metadata references unowned module {module}: {metadata_path}")


def verify_compiler_launcher_bypass(log: Path, source: Path) -> None:
    if not log.is_file() or not log.read_text(encoding="utf-8").strip():
        raise RuntimeError("compiler launcher regression did not exercise the guest build")
    invocations = log.read_text(encoding="utf-8")
    library_sources = (
        source / "include/product/chain/values.cppm",
        source / "include/product/chain/limits.cppm",
        source / "include/product/chain/protocol.cppm",
        source / "src/limits.cpp",
        source / "src/protocol.cpp",
    )
    for library_source in library_sources:
        if str(library_source.resolve()) in invocations:
            raise RuntimeError(f"contract-library compilation used a compiler cache launcher: {library_source}")


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
            "unregistered-imported-library",
            """add_library(unregistered STATIC IMPORTED)
set_target_properties(
   unregistered
   PROPERTIES
      FORGE_CONTRACT_LIBRARY TRUE
      FORGE_CONTRACT_LIBRARY_ID negative.unregistered
)
forge_add_contract_library(
   protocol ID negative.imported.consumer SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES unregistered
)
""",
            "its package config",
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
            "library-component-id",
            """forge_add_contract_library(
   protocol ID forge.raw SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES Forge::forge_raw
)
""",
            "shared by a library and component",
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
        (
            "immutable-concrete-sources",
            """forge_add_contract_library(
   protocol ID negative.immutable.sources SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
get_target_property(concrete protocol ALIASED_TARGET)
target_sources("${concrete}" PRIVATE src/protocol.cpp)
""",
            "modified after descriptor declaration: SOURCES",
        ),
        (
            "immutable-concrete-libraries",
            """add_library(extra_dependency INTERFACE)
forge_add_contract_library(
   protocol ID negative.immutable.libraries SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
get_target_property(concrete protocol ALIASED_TARGET)
target_link_libraries("${concrete}" PRIVATE extra_dependency)
""",
            "modified after descriptor declaration: LINK_LIBRARIES",
        ),
        (
            "immutable-concrete-module-scanning",
            """forge_add_contract_library(
   protocol ID negative.immutable.module.scanning SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   SOURCES src/protocol.cpp
)
get_target_property(concrete protocol ALIASED_TARGET)
set_property(TARGET "${concrete}" PROPERTY CXX_SCAN_FOR_MODULES OFF)
""",
            "modified after descriptor declaration: CXX_SCAN_FOR_MODULES",
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


def check_imported_target_seal(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    product_package: Path,
    output: Path,
) -> None:
    output.mkdir(parents=True)
    (output / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(ImportedContractMutation LANGUAGES CXX)
find_package(ForgeContract CONFIG REQUIRED)
find_package(ProductProtocol CONFIG REQUIRED)
add_library(host_only INTERFACE)
target_link_libraries(Product::protocol INTERFACE host_only)
""",
        encoding="utf-8",
    )
    configure(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_package=product_package,
        source=output,
        build=output / "build",
        succeeds=False,
        contains="descriptor declaration: INTERFACE_LINK_LIBRARIES",
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

    external = write_project(
        fixtures / "external-header",
        """forge_add_contract_library(
   protocol ID negative.external SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_BASE_DIRS . MODULE_SOURCES protocol.cppm
)
forge_add_contract(
   negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/contract"
   SOURCES contract.cpp LIBRARIES protocol
)
""",
    )
    (external / "shared.hpp").write_text(
        "#pragma once\ninline constexpr auto shared_value = 42;\n",
        encoding="utf-8",
    )
    (external / "contract").mkdir()
    (external / "contract.cpp").replace(external / "contract" / "contract.cpp")
    (external / "contract" / "contract.cpp").write_text(
        """#include "../shared.hpp"
import forge.contract;
import negative.protocol;

class [[forge::contract("negative")]] negative_contract : public forge::contract::context {
 public:
   using context::context;
   [[forge::action]] void verify() {
      forge::contract::check(shared_value == protocol_value, "invalid value");
   }
};
""",
        encoding="utf-8",
    )

    owner_private = write_project(
        fixtures / "owner-private-header",
        """forge_add_contract_library(
   protocol ID negative.owner.private SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PRIVATE_HEADERS private/detail.hpp
)
forge_add_contract(negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES contract.cpp LIBRARIES protocol)
""",
    )
    (owner_private / "private").mkdir()
    (owner_private / "private" / "detail.hpp").write_text(
        "#pragma once\ninline constexpr auto private_value = 42;\n",
        encoding="utf-8",
    )
    (owner_private / "include" / "protocol.cppm").write_text(
        """module;
#include "../private/detail.hpp"
export module negative.protocol;
export inline constexpr auto protocol_value = private_value;
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

    private_module_import = write_project(
        fixtures / "private-module-import",
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
    (private_module_import / "private").mkdir()
    (private_module_import / "private" / "detail.cppm").write_text(
        "export module negative.detail;\nexport inline constexpr auto private_value = 42;\n",
        encoding="utf-8",
    )
    (private_module_import / "include" / "protocol.cppm").write_text(
        """export module negative.protocol;
import negative.detail;
export inline constexpr auto protocol_value = private_value;
""",
        encoding="utf-8",
    )

    private_header_include = write_project(
        fixtures / "private-header-include",
        """forge_add_contract_library(
   private_library ID negative.private SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/private"
   MODULE_BASE_DIRS . MODULE_SOURCES detail.cppm
   PUBLIC_HEADERS detail.hpp
)
forge_add_contract_library(
   protocol ID negative.protocol SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PRIVATE_LIBRARIES private_library
)
forge_add_contract(negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES contract.cpp LIBRARIES protocol)
""",
    )
    (private_header_include / "private").mkdir()
    (private_header_include / "private" / "detail.cppm").write_text(
        "export module negative.detail;\n",
        encoding="utf-8",
    )
    (private_header_include / "private" / "detail.hpp").write_text(
        "#pragma once\ninline constexpr auto private_value = 42;\n",
        encoding="utf-8",
    )
    (private_header_include / "include" / "protocol.cppm").write_text(
        """module;
#include "detail.hpp"
export module negative.protocol;
export inline constexpr auto protocol_value = private_value;
""",
        encoding="utf-8",
    )

    cases = [
        (undeclared, "contract source dependency is not declared"),
        (external, "contract source dependency is not declared"),
        (owner_private, "contract public module uses a private source"),
        (private_import, "module 'negative.detail' not found"),
        (private_export, "exports a module through a private dependency"),
        (private_module_import, "imports a module through an undeclared dependency"),
        (private_header_include, "source dependency owner is not visible"),
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
    compiler_launcher = output / "compiler-launcher.sh"
    compiler_launcher_log = output / "compiler-launcher.log"
    compiler_launcher.write_text(
        '#!/bin/sh\nprintf "%s\\n" "$*" >> "$FORGE_TEST_COMPILER_LAUNCHER_LOG"\nexec "$@"\n',
        encoding="utf-8",
    )
    compiler_launcher.chmod(0o755)
    compiler_environment = os.environ.copy()
    compiler_environment["CMAKE_CXX_COMPILER_LAUNCHER"] = str(compiler_launcher)
    compiler_environment["FORGE_TEST_COMPILER_LAUNCHER_LOG"] = str(compiler_launcher_log)
    configure(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source / "producer",
        build=producer_build,
        definitions=(
            f"-DCMAKE_INSTALL_PREFIX={producer_install}",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=",
        ),
        environment=compiler_environment,
    )
    build(cmake, producer_build, environment=compiler_environment)
    run(str(producer_build / "product_protocol_host_tests"))
    abi, manifest = verify_artifacts(producer_build, "product")
    verify_direct_action(abi)
    verify_source_graph(manifest, read_json(producer_build / "product.contract-graph.json"))
    module_owners, source_owners = verify_contract_graph(producer_build)
    verify_component_module_metadata(producer_build, module_owners)
    dependency_file = next(
        path
        for path in component_metadata_files(producer_build)
        if any(rule.get("provides", []) for rule in read_json(path).get("rules", []))
    )
    duplicate_dependency_file = dependency_file.with_name(f"duplicate-{dependency_file.name}")
    shutil.copy2(dependency_file, duplicate_dependency_file)
    try:
        verify_component_module_metadata(producer_build, module_owners)
    finally:
        duplicate_dependency_file.unlink()

    source_target = component_metadata_target(dependency_file)
    wrong_owner_target = next(
        path
        for path in {
            candidate.parent
            for candidate in component_metadata_files(producer_build)
            if component_metadata_target(candidate) != source_target
        }
    )
    wrong_owner_dependency_file = wrong_owner_target / f"wrong-owner-{dependency_file.name}"
    shutil.copy2(dependency_file, wrong_owner_dependency_file)
    try:
        try:
            verify_component_module_metadata(producer_build, module_owners)
        except RuntimeError as error:
            if "under the wrong owner" not in str(error):
                raise
        else:
            raise RuntimeError("component metadata ownership check accepted a wrong-owner module")
    finally:
        wrong_owner_dependency_file.unlink()
    verify_compilation_metadata(producer_build, module_owners, source_owners)
    verify_compiler_launcher_bypass(compiler_launcher_log, source / "producer")
    initial_digest = manifest["source_graph"]["sha256"]
    build(cmake, producer_build, environment=compiler_environment)
    if verify_artifacts(producer_build, "product")[1]["source_graph"]["sha256"] != initial_digest:
        raise RuntimeError("source graph digest changed across an incremental rebuild")

    graph_path = producer_build / "product.contract-graph.json"
    abi_path = producer_build / "product.abi"
    original_graph = graph_path.read_text(encoding="utf-8")
    graph = json.loads(original_graph)
    component = next(entry for entry in graph["components"] if entry["id"] == "forge.contract.runtime")
    component["modules"].append("forge.contract.synthetic")
    previous_abi_mtime_ns = abi_path.stat().st_mtime_ns
    write_json_newer_than(graph_path, graph, previous_abi_mtime_ns)
    build(cmake, producer_build, environment=compiler_environment)
    _, changed_manifest = verify_artifacts(producer_build, "product")
    if abi_path.stat().st_mtime_ns <= previous_abi_mtime_ns:
        raise RuntimeError("ABI generation did not rerun after a contract graph change")
    if changed_manifest["source_graph"]["sha256"] == initial_digest:
        raise RuntimeError("contract manifest did not rerun after a contract graph change")
    verify_source_graph(changed_manifest, graph)

    changed_abi_mtime_ns = abi_path.stat().st_mtime_ns
    graph_path.write_text(original_graph, encoding="utf-8")
    current_graph = graph_path.stat()
    if current_graph.st_mtime_ns <= changed_abi_mtime_ns:
        os.utime(graph_path, ns=(current_graph.st_atime_ns, changed_abi_mtime_ns + 1))
    build(cmake, producer_build, environment=compiler_environment)
    if verify_artifacts(producer_build, "product")[1]["source_graph"]["sha256"] != initial_digest:
        raise RuntimeError("source graph digest did not recover after restoring the descriptor")

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
    check_imported_target_seal(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_package=product_package,
        output=output / "imported-target-seal",
    )

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
    verify_artifacts(consumer_build / "contract", "consumer")

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

#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(
    *command: str,
    succeeds: bool = True,
    contains: str | None = None,
) -> str:
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


def configure_host(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    source: Path,
    build: Path,
    prefixes: tuple[Path, ...] = (),
    definitions: tuple[str, ...] = (),
    succeeds: bool = True,
    contains: str | None = None,
) -> None:
    prefix_path = ";".join(str(path) for path in prefixes)
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
    if prefix_path:
        command.append(f"-DCMAKE_PREFIX_PATH={prefix_path}")
    if (sysroot := macos_sysroot()) is not None:
        command.append(f"-DCMAKE_OSX_SYSROOT={sysroot}")
    command.extend(definitions)
    run(*command, succeeds=succeeds, contains=contains)


def configure_guest(
    *,
    cmake: str,
    contract_package: Path,
    source: Path,
    build: Path,
    artifact_directory: Path | None = None,
    prefixes: tuple[Path, ...] = (),
    definitions: tuple[str, ...] = (),
    succeeds: bool = True,
    contains: str | None = None,
) -> None:
    sdk_prefix = contract_package.parent.parent.parent
    prefix_values = [str(path) for path in prefixes]
    prefix_values.append(str(sdk_prefix))
    command = [
        cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DCMAKE_TOOLCHAIN_FILE={contract_package / 'ForgeContractToolchain.cmake'}",
        f"-DForgeContract_DIR={contract_package}",
        f"-DCMAKE_PREFIX_PATH={';'.join(prefix_values)}",
    ]
    if artifact_directory is not None:
        command.append(f"-DFORGE_CONTRACT_ARTIFACT_DIR={artifact_directory}")
    command.extend(definitions)
    run(*command, succeeds=succeeds, contains=contains)


def configure_multi_config_host(
    *,
    cmake: str,
    cxx_compiler: Path,
    contract_package: Path,
    source: Path,
    build: Path,
) -> None:
    command = [
        cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja Multi-Config",
        "-DCMAKE_CONFIGURATION_TYPES=Debug;Release;Checked",
        "-DCMAKE_NO_SYSTEM_FROM_IMPORTED=ON",
        f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
        f"-DForgeContract_DIR={contract_package}",
    ]
    if (sysroot := macos_sysroot()) is not None:
        command.append(f"-DCMAKE_OSX_SYSROOT={sysroot}")
    run(*command)


def build(
    cmake: str,
    directory: Path,
    *targets: str,
    configuration: str | None = None,
    succeeds: bool = True,
    contains: str | None = None,
) -> None:
    command = [cmake, "--build", str(directory), "-j", "4"]
    if configuration is not None:
        command.extend(["--config", configuration])
    if targets:
        command.extend(["--target", *targets])
    run(*command, succeeds=succeeds, contains=contains)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def verify_artifacts(directory: Path, contract: str) -> tuple[dict, dict]:
    for suffix in ("wasm", "abi", "contract.json"):
        artifact = directory / f"{contract}.{suffix}"
        if not artifact.is_file() or artifact.stat().st_size == 0:
            raise RuntimeError(f"missing contract artifact: {artifact}")
    return read_json(directory / f"{contract}.abi"), read_json(directory / f"{contract}.contract.json")


def verify_identical_artifacts(left: Path, right: Path, contract: str) -> None:
    for suffix in ("wasm", "abi", "contract.json"):
        left_path = left / f"{contract}.{suffix}"
        right_path = right / f"{contract}.{suffix}"
        if left_path.read_bytes() != right_path.read_bytes():
            raise RuntimeError(f"direct and helper builds differ: {suffix}")


def verify_multi_config_forwarding(
    *,
    cmake: str,
    cxx_compiler: Path,
    contract_package: Path,
    source: Path,
    output: Path,
) -> None:
    build_directory = output / "multi-config-host"
    configure_multi_config_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        contract_package=contract_package,
        source=source / "multi_config",
        build=build_directory,
    )
    build(cmake, build_directory, "configuration_guest", configuration="Release")
    build(cmake, build_directory, "configuration_guest", configuration="Checked")

    artifact_directory = build_directory / "configuration.guest" / "artifacts"
    release_marker = artifact_directory / "built-Release.txt"
    checked_marker = artifact_directory / "built-Checked.txt"
    debug_marker = artifact_directory / "built-Debug.txt"
    if release_marker.read_text(encoding="utf-8").strip() != "Release":
        raise RuntimeError("host Release did not select the guest Release configuration")
    if debug_marker.exists():
        raise RuntimeError("host Release unexpectedly built the guest Debug configuration")
    if checked_marker.read_text(encoding="utf-8").strip() != "Checked":
        raise RuntimeError("host Checked configuration was not forwarded to the guest project")
    verify_artifacts(artifact_directory, "configuration")


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
        raise RuntimeError("contract manifest does not use schema v2")
    graph = manifest["source_graph"]
    if graph["root_owner"] != descriptor["root"]["owner"]:
        raise RuntimeError("source graph has the wrong root owner")

    expected_roles = {
        "product.chain.values": {"module", "public_header"},
        "product.chain.limits": {"module", "implementation"},
        "product.chain.protocol": {"module", "implementation", "private_header"},
        "product.contract.storage": {"module", "implementation"},
        "product.contract.revision": {"module", "implementation"},
    }
    for owner, expected in expected_roles.items():
        observed = {entry["role"] for entry in graph["files"] if entry["owner"] == owner}
        if observed != expected:
            raise RuntimeError(
                f"source graph roles for {owner} are {sorted(observed)}, expected {sorted(expected)}"
            )

    observed_edges = {
        (entry["owner"], entry["kind"], entry["dependency"], entry["scope"])
        for entry in graph["dependencies"]
    }
    expected_edges = {
        ("contract:product", "library", "product.contract.revision", "public"),
        ("product.contract.revision", "library", "product.chain.protocol", "public"),
        ("product.contract.revision", "library", "product.contract.storage", "private"),
        ("product.chain.protocol", "library", "product.chain.values", "public"),
        ("product.chain.protocol", "library", "product.chain.limits", "private"),
        ("product.chain.limits", "component", "forge.crypto.digest", "public"),
    }
    if not expected_edges <= observed_edges:
        raise RuntimeError(f"source graph omits dependency edges: {sorted(expected_edges - observed_edges)}")

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
    for dependency in graph["dependencies"]:
        append_field(encoded, "dependency")
        append_field(encoded, dependency["owner"])
        append_field(encoded, dependency["kind"])
        append_field(encoded, dependency["dependency"])
        append_field(encoded, dependency["scope"])
    append_length(encoded, len(graph["components"]))
    for component in graph["components"]:
        append_field(encoded, "component")
        append_field(encoded, component["id"])
        append_length(encoded, len(component["modules"]))
        for module in component["modules"]:
            append_field(encoded, module)
    if graph["sha256"] != hashlib.sha256(encoded).hexdigest():
        raise RuntimeError("source graph digest does not cover its canonical semantic fields")


def verify_contract_graph(build_directory: Path) -> None:
    graph = read_json(build_directory / "product.contract-graph.json")
    if graph["schema"] != 2:
        raise RuntimeError("contract graph does not use schema v2")
    if graph["root"]["components"] != ["forge.contract.runtime"]:
        raise RuntimeError("contract graph has unexpected foundation components")
    if graph["root"]["libraries"] != ["product.contract.revision"]:
        raise RuntimeError("contract graph has the wrong root library")

    components = {entry["id"]: entry for entry in graph["components"]}
    runtime = components.get("forge.contract.runtime")
    if runtime is None:
        raise RuntimeError("contract graph omits the Forge Contract runtime")
    expected_runtime_dependencies = [
        "forge.raw",
        "forge.codec.base64",
        "forge.codec.base58",
        "forge.codec.hex",
        "forge.chain.protocol",
    ]
    if runtime["dependencies"] != expected_runtime_dependencies:
        raise RuntimeError("contract runtime dependency graph is incomplete")

    module_owners: dict[str, str] = {}
    for component_id, component in components.items():
        for module in component["modules"]:
            if module in module_owners:
                raise RuntimeError(f"contract graph gives module {module} multiple owners")
            module_owners[module] = component_id
    for required in ("forge.contract", "forge.contract.multi_index", "forge.crypto.digest.sha256"):
        if required not in module_owners:
            raise RuntimeError(f"contract graph does not own exposed SDK module {required}")


def verify_relocatable_package(prefix: Path, forbidden: tuple[Path, ...]) -> None:
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


def verify_guest_package_diamond(
    *,
    cmake: str,
    contract_package: Path,
    product_prefixes: tuple[Path, ...],
    product_package: Path,
    values_package: Path,
    output: Path,
) -> None:
    source = output / "source"
    build_directory = output / "build"
    shared_source = source / "shared"
    shared_source.mkdir(parents=True)
    (shared_source / "CMakeLists.txt").write_text(
        """find_package(ProductValues CONFIG REQUIRED)
""",
        encoding="utf-8",
    )
    for side in ("left", "right"):
        side_source = source / side
        include = side_source / "include"
        include.mkdir(parents=True)
        (include / f"{side}.cppm").write_text(
            f"""export module package.diamond.{side};

import product.chain.protocol;

export namespace package::diamond {{

inline bool {side}_valid() {{
   const auto value = product::chain::checked_add(20, 22);
   return value && *value == 42;
}}

}} // namespace package::diamond
""",
            encoding="utf-8",
        )
        (side_source / "CMakeLists.txt").write_text(
            f"""find_package(ProductProtocol CONFIG REQUIRED)

forge_add_contract_library(
   diamond_{side}
   ID package.diamond.{side}
   SOURCE_ROOT "${{CMAKE_CURRENT_SOURCE_DIR}}"
   MODULE_BASE_DIRS include
   MODULE_SOURCES include/{side}.cppm
   PUBLIC_LIBRARIES Product::protocol
)
""",
            encoding="utf-8",
        )

    (source / "entry.cpp").write_text(
        """import forge.contract;
import package.diamond.left;
import package.diamond.right;

class [[forge::contract("pkgdiamond")]] package_diamond_contract final
    : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void verify() {
      forge::contract::check(
          package::diamond::left_valid() && package::diamond::right_valid(),
          "installed package dependency diamond is invalid");
   }
};
""",
        encoding="utf-8",
    )
    (source / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(InstalledContractPackageDiamond LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)
add_subdirectory(shared)
add_subdirectory(left)
add_subdirectory(right)

forge_add_contract(
   pkgdiamond
   SOURCES entry.cpp
   LIBRARIES diamond_left diamond_right
)
""",
        encoding="utf-8",
    )

    configure_guest(
        cmake=cmake,
        contract_package=contract_package,
        source=source,
        build=build_directory,
        prefixes=product_prefixes,
        definitions=(
            f"-DProductProtocol_DIR={product_package}",
            f"-DProductValues_DIR={values_package}",
        ),
    )
    build(cmake, build_directory, "pkgdiamond_artifacts")
    verify_artifacts(build_directory / "artifacts", "pkgdiamond")


def verify_native_package_diamond(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    product_prefixes: tuple[Path, ...],
    product_package: Path,
    values_package: Path,
    output: Path,
) -> None:
    source = output / "source"
    build_directory = output / "build"
    for side, package in (
        ("left", "ProductValues"),
        ("right", "ProductProtocol"),
    ):
        side_source = source / side
        side_source.mkdir(parents=True)
        body = f"find_package({package} CONFIG REQUIRED)\n"
        if side == "right":
            body += """
add_executable(native_package_diamond ../main.cpp)
target_link_libraries(
   native_package_diamond
   PRIVATE
      Values::values
      Product::values
)
"""
        (side_source / "CMakeLists.txt").write_text(body, encoding="utf-8")
    (source / "main.cpp").write_text(
        """import product.chain.values;

int main() {
   return product::chain::workspace_id{42}.value == 42 ? 0 : 1;
}
""",
        encoding="utf-8",
    )
    (source / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(NativeContractPackageDiamond LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)
add_subdirectory(left)
add_subdirectory(right)
""",
        encoding="utf-8",
    )
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source,
        build=build_directory,
        prefixes=product_prefixes,
        definitions=(
            f"-DProductProtocol_DIR={product_package}",
            f"-DProductValues_DIR={values_package}",
        ),
    )
    build(cmake, build_directory, "native_package_diamond")
    run(str(build_directory / "right" / "native_package_diamond"))


def verify_keyword_like_descriptor_values(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    output: Path,
) -> None:
    producer = output / "producer"
    module_directory = producer / "CANONICAL_SOURCES"
    module_directory.mkdir(parents=True)
    (module_directory / "protocol.cppm").write_text(
        """export module product.keyword.protocol;
export inline constexpr int keyword_protocol_value = 42;
""",
        encoding="utf-8",
    )
    (producer / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(KeywordDescriptorProducer LANGUAGES CXX)
include(GNUInstallDirs)
find_package(Forge CONFIG REQUIRED)
find_package(ForgeContract CONFIG REQUIRED)
forge_add_contract_library(
   keyword_protocol
   ID product.keyword.protocol
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS CANONICAL_SOURCES
   MODULE_SOURCES CANONICAL_SOURCES/protocol.cppm
   PUBLIC_LIBRARIES Forge::forge_raw
)
forge_install_contract_library(
   TARGET keyword_protocol
   EXPORT KeywordProtocolTargets
   EXPORT_NAME protocol
   MODULE_DESTINATION "${CMAKE_INSTALL_DATADIR}/keyword-protocol/modules"
   SOURCE_DESTINATION "${CMAKE_INSTALL_DATADIR}/keyword-protocol/sources"
)
install(
   EXPORT KeywordProtocolTargets
   FILE KeywordProtocolTargets.cmake
   NAMESPACE Keyword::
   DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/KeywordProtocol"
)
forge_install_contract_package(
   EXPORT KeywordProtocolTargets
   FILE KeywordProtocolContract.cmake
   NAMESPACE Keyword::
   DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/KeywordProtocol"
   TARGETS keyword_protocol
)
""",
        encoding="utf-8",
    )
    install_prefix = output / "install"
    producer_build = output / "producer-build"
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=producer,
        build=producer_build,
        definitions=(f"-DCMAKE_INSTALL_PREFIX={install_prefix}",),
    )
    build(cmake, producer_build, "all")
    run(cmake, "--install", str(producer_build))

    consumer = output / "consumer"
    consumer.mkdir()
    contract_file = (
        install_prefix
        / "lib"
        / "cmake"
        / "KeywordProtocol"
        / "KeywordProtocolContract.cmake"
    )
    (consumer / "entry.cpp").write_text(
        """import forge.contract;
import product.keyword.protocol;

class [[forge::contract("keyword")]] keyword_contract
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]]
   void verify() {
      forge::contract::check(keyword_protocol_value == 42, "invalid value");
   }
};
""",
        encoding="utf-8",
    )
    (consumer / "CMakeLists.txt").write_text(
        f"""cmake_minimum_required(VERSION 3.31)
project(KeywordDescriptorConsumer LANGUAGES CXX)
find_package(ForgeContract CONFIG REQUIRED)
include("{contract_file.as_posix()}")
forge_add_contract(
   keyword
   SOURCE_ROOT "${{CMAKE_CURRENT_SOURCE_DIR}}"
   SOURCES entry.cpp
   LIBRARIES Keyword::protocol
)
""",
        encoding="utf-8",
    )
    consumer_build = output / "consumer-build"
    configure_guest(
        cmake=cmake,
        contract_package=contract_package,
        source=consumer,
        build=consumer_build,
        prefixes=(install_prefix,),
    )
    build(cmake, consumer_build, "keyword_artifacts")
    verify_artifacts(consumer_build / "artifacts", "keyword")


def verify_conflicting_package_descriptor(
    *,
    cmake: str,
    contract_package: Path,
    product_prefix: Path,
    values_package: Path,
    output: Path,
) -> None:
    conflicting_prefix = output / "conflicting-prefix"
    shutil.copytree(product_prefix, conflicting_prefix)
    conflicting_package = conflicting_prefix / "lib" / "cmake" / "ProductProtocol"
    contract_file = conflicting_package / "ProductProtocolContract.cmake"
    contract_text = contract_file.read_text(encoding="utf-8")
    contract_text, replacements = re.subn(
        r"(FINGERPRINT )[0-9a-f]{64}",
        r"\g<1>" + ("0" * 64),
        contract_text,
        count=1,
    )
    if replacements != 1:
        raise RuntimeError("installed protocol package has no materialization fingerprint")
    contract_file.write_text(contract_text, encoding="utf-8")

    source = output / "source"
    source.mkdir(parents=True)
    (source / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(ConflictingContractPackages LANGUAGES CXX)
find_package(ForgeContract CONFIG REQUIRED)
find_package(ProductValues CONFIG REQUIRED)
find_package(ProductProtocol CONFIG REQUIRED)
""",
        encoding="utf-8",
    )
    configure_guest(
        cmake=cmake,
        contract_package=contract_package,
        source=source,
        build=output / "build",
        prefixes=(product_prefix, conflicting_prefix),
        definitions=(
            f"-DProductProtocol_DIR={conflicting_package}",
            f"-DProductValues_DIR={values_package}",
        ),
        succeeds=False,
        contains="conflicting installed Forge Contract package descriptor",
    )


def verify_conflicting_package_materialization(
    *,
    cmake: str,
    cxx_compiler: Path,
    forge_package: Path,
    contract_package: Path,
    product_prefix: Path,
    values_package: Path,
    output: Path,
) -> None:
    conflicting_prefix = output / "conflicting-prefix"
    shutil.copytree(product_prefix, conflicting_prefix)
    protocol_module = (
        conflicting_prefix
        / "share"
        / "product-protocol"
        / "modules"
        / "product"
        / "chain"
        / "protocol.cppm"
    )
    protocol_module.write_text(
        protocol_module.read_text(encoding="utf-8") + "\n// tampered package input\n",
        encoding="utf-8",
    )
    conflicting_config = (
        conflicting_prefix
        / "lib"
        / "cmake"
        / "ProductProtocol"
        / "ProductProtocolConfig.cmake"
    )

    guest_source = output / "guest-source"
    guest_source.mkdir(parents=True)
    (guest_source / "CMakeLists.txt").write_text(
        f"""cmake_minimum_required(VERSION 3.31)
project(ConflictingContractPackageMaterialization LANGUAGES CXX)
find_package(ForgeContract CONFIG REQUIRED)
find_package(ProductValues CONFIG REQUIRED)
find_package(ProductProtocol CONFIG REQUIRED)
include("{conflicting_config.as_posix()}")
""",
        encoding="utf-8",
    )
    product_package = product_prefix / "lib" / "cmake" / "ProductProtocol"
    configure_guest(
        cmake=cmake,
        contract_package=contract_package,
        source=guest_source,
        build=output / "guest-build",
        prefixes=(product_prefix,),
        definitions=(
            f"-DProductProtocol_DIR={product_package}",
            f"-DProductValues_DIR={values_package}",
        ),
        succeeds=False,
        contains="conflicting installed Forge Contract package descriptor",
    )

    native_source = output / "native-source"
    native_source.mkdir()
    (native_source / "CMakeLists.txt").write_text(
        f"""cmake_minimum_required(VERSION 3.31)
project(ConflictingNativePackageMaterialization LANGUAGES CXX)
find_package(Forge CONFIG REQUIRED)
find_package(ForgeContract CONFIG REQUIRED)
find_package(ProductValues CONFIG REQUIRED)
find_package(ProductProtocol CONFIG REQUIRED)
include("{conflicting_config.as_posix()}")
""",
        encoding="utf-8",
    )
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=native_source,
        build=output / "native-build",
        prefixes=(product_prefix,),
        definitions=(
            f"-DProductProtocol_DIR={product_package}",
            f"-DProductValues_DIR={values_package}",
        ),
        succeeds=False,
        contains="conflicting installed Forge Contract package descriptor",
    )


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
    (root / "entry.cpp").write_text(
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
find_package(ForgeContract CONFIG REQUIRED)
"""
        + body,
        encoding="utf-8",
    )
    return root


def check_configure_failures(
    *,
    cmake: str,
    contract_package: Path,
    output: Path,
) -> None:
    fixtures = output / "configure"
    outside = fixtures / "outside.cppm"
    outside.parent.mkdir(parents=True)
    outside.write_text("export module negative.outside;\n", encoding="utf-8")

    ancestor_mutation = fixtures / "immutable-ancestor-mutation"
    ancestor_library = ancestor_mutation / "library"
    (ancestor_library / "include").mkdir(parents=True)
    (ancestor_library / "include" / "protocol.cppm").write_text(
        "export module negative.ancestor;\n",
        encoding="utf-8",
    )
    (ancestor_mutation / "extra.cpp").write_text(
        "inline constexpr auto ancestor_mutation = true;\n",
        encoding="utf-8",
    )
    (ancestor_library / "CMakeLists.txt").write_text(
        """forge_add_contract_library(
   protocol ID negative.immutable.ancestor SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
""",
        encoding="utf-8",
    )
    (ancestor_mutation / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.31)
project(AncestorContractMutation LANGUAGES CXX)
find_package(ForgeContract CONFIG REQUIRED)
add_subdirectory(library)
get_target_property(concrete protocol ALIASED_TARGET)
target_sources("${concrete}" PRIVATE extra.cpp)
""",
        encoding="utf-8",
    )
    configure_guest(
        cmake=cmake,
        contract_package=contract_package,
        source=ancestor_mutation,
        build=fixtures / "immutable-ancestor-mutation-build",
        succeeds=False,
        contains="modified after descriptor declaration: SOURCES",
    )

    cases = (
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
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/second")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/second/protocol.cppm" "export module negative.second;\\n")
forge_add_contract_library(
   second ID negative.duplicate.id SOURCE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/second"
   MODULE_BASE_DIRS . MODULE_SOURCES protocol.cppm
)
""",
            "duplicate Forge Contract library ID",
        ),
        (
            "duplicate-dependency",
            """forge_add_contract_library(
   protocol ID negative.duplicate.dependency SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES Forge::forge_raw Forge::forge_raw
)
""",
            "duplicate PUBLIC Forge guest component dependency",
        ),
        (
            "conflicting-scope",
            """forge_add_contract_library(
   protocol ID negative.conflicting.scope SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES Forge::forge_raw
   PRIVATE_LIBRARIES Forge::forge_raw
)
""",
            "conflicting scopes",
        ),
        (
            "unknown-component",
            """add_library(unknown_component STATIC IMPORTED GLOBAL)
set_target_properties(
   unknown_component PROPERTIES FORGE_CONTRACT_GUEST_COMPONENT_ID forge.unknown
)
forge_add_contract_library(
   protocol ID negative.unknown.component SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
   PUBLIC_LIBRARIES unknown_component
)
""",
            "unknown Forge Contract guest component descriptor",
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
            "immutable-interprocedural-optimization",
            """forge_add_contract_library(
   protocol ID negative.immutable.ipo SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
get_target_property(concrete protocol ALIASED_TARGET)
set_property(TARGET "${concrete}" PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
""",
            "modified after descriptor declaration: INTERPROCEDURAL_OPTIMIZATION",
        ),
        (
            "immutable-static-library-options",
            """forge_add_contract_library(
   protocol ID negative.immutable.static.options SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
get_target_property(concrete protocol ALIASED_TARGET)
set_property(TARGET "${concrete}" PROPERTY STATIC_LIBRARY_OPTIONS "unexpected")
""",
            "modified after descriptor declaration: STATIC_LIBRARY_OPTIONS",
        ),
        (
            "immutable-source-options",
            """forge_add_contract_library(
   protocol ID negative.immutable.source.options SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
get_target_property(concrete protocol ALIASED_TARGET)
set_source_files_properties(
   "${CMAKE_CURRENT_SOURCE_DIR}/include/protocol.cppm"
   TARGET_DIRECTORY "${concrete}"
   PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/extra.hpp"
)
""",
            "uses unsupported source property: COMPILE_OPTIONS",
        ),
        (
            "immutable-late-deferred-mutation",
            """forge_add_contract_library(
   protocol ID negative.immutable.late.deferred SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS include MODULE_SOURCES include/protocol.cppm
)
function(mutate_contract_target)
   get_target_property(concrete protocol ALIASED_TARGET)
   set_property(TARGET "${concrete}" PROPERTY CXX_SCAN_FOR_MODULES OFF)
endfunction()
cmake_language(DEFER CALL mutate_contract_target)
""",
            "modified after descriptor declaration: CXX_SCAN_FOR_MODULES",
        ),
        (
            "cycle",
            """add_library(cycle_a STATIC IMPORTED GLOBAL)
add_library(cycle_b STATIC IMPORTED GLOBAL)
set_target_properties(
   cycle_a PROPERTIES
      FORGE_CONTRACT_LIBRARY TRUE
      FORGE_CONTRACT_LIBRARY_ID negative.cycle.a
      FORGE_CONTRACT_PUBLIC_LIBRARY_IDS negative.cycle.b
      FORGE_CONTRACT_PRIVATE_LIBRARY_IDS ""
      FORGE_CONTRACT_PUBLIC_COMPONENT_IDS ""
      FORGE_CONTRACT_PRIVATE_COMPONENT_IDS ""
      FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE "."
      FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE "."
      FORGE_CONTRACT_INSTALL_MODULE_PATHS ""
      FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS ""
      IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/libcycle_a.a"
)
set_target_properties(
   cycle_b PROPERTIES
      FORGE_CONTRACT_LIBRARY TRUE
      FORGE_CONTRACT_LIBRARY_ID negative.cycle.b
      FORGE_CONTRACT_PUBLIC_LIBRARY_IDS negative.cycle.a
      FORGE_CONTRACT_PRIVATE_LIBRARY_IDS ""
      FORGE_CONTRACT_PUBLIC_COMPONENT_IDS ""
      FORGE_CONTRACT_PRIVATE_COMPONENT_IDS ""
      FORGE_CONTRACT_INSTALL_MODULE_ROOT_RELATIVE "."
      FORGE_CONTRACT_INSTALL_SOURCE_ROOT_RELATIVE "."
      FORGE_CONTRACT_INSTALL_MODULE_PATHS ""
      FORGE_CONTRACT_INSTALL_PUBLIC_HEADER_PATHS ""
      IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/libcycle_b.a"
)
forge_register_contract_library_targets(cycle_a cycle_b)
forge_add_contract(
   negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   SOURCES entry.cpp LIBRARIES cycle_a
)
""",
            "cycle in Forge Contract library dependencies",
        ),
    )
    for name, body, diagnostic in cases:
        source = write_project(fixtures / name, body)
        configure_guest(
            cmake=cmake,
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
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=output,
        build=output / "build",
        definitions=(f"-DProductProtocol_DIR={product_package}",),
        succeeds=False,
        contains="descriptor declaration: INTERFACE_LINK_LIBRARIES",
    )


def check_build_failures(
    *,
    cmake: str,
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
forge_add_contract(
   negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   SOURCES entry.cpp LIBRARIES protocol
)
""",
    )
    (undeclared / "hidden.hpp").write_text(
        "#pragma once\ninline constexpr auto hidden_value = 42;\n",
        encoding="utf-8",
    )
    (undeclared / "entry.cpp").write_text(
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
forge_add_contract(
   negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   SOURCES entry.cpp LIBRARIES protocol
)
""",
    )
    (private_import / "private").mkdir()
    (private_import / "private" / "detail.cppm").write_text(
        "export module negative.detail;\nexport inline constexpr auto private_value = 42;\n",
        encoding="utf-8",
    )
    (private_import / "entry.cpp").write_text(
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
forge_add_contract(
   negative SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   SOURCES entry.cpp LIBRARIES protocol
)
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

    cases = (
        (undeclared, "contract source dependency is not declared"),
        (private_import, "contract imports a module through an undeclared library"),
        (private_export, "exports a module through a private dependency"),
    )
    for source, diagnostic in cases:
        build_directory = fixtures / f"{source.name}-build"
        configure_guest(
            cmake=cmake,
            contract_package=contract_package,
            source=source,
            build=build_directory,
        )
        build(
            cmake,
            build_directory,
            "negative_artifacts",
            succeeds=False,
            contains=diagnostic,
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

    verify_multi_config_forwarding(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        contract_package=contract_package,
        source=source,
        output=output,
    )

    producer_build = output / "producer-host"
    producer_install = output / "producer-install"
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source / "producer",
        build=producer_build,
        definitions=(f"-DCMAKE_INSTALL_PREFIX={producer_install}",),
    )
    build(cmake, producer_build, "product_protocol_host_tests")
    run(str(producer_build / "product_protocol_host_tests"))
    build(cmake, producer_build, "product_guest")

    helper_build = producer_build / "product.guest"
    helper_artifacts = helper_build / "artifacts"
    helper_abi, helper_manifest = verify_artifacts(helper_artifacts, "product")
    helper_graph = read_json(helper_build / "product.contract-graph.json")
    verify_direct_action(helper_abi)
    verify_source_graph(helper_manifest, helper_graph)
    verify_contract_graph(helper_build)

    direct_build = output / "producer-direct-guest"
    direct_artifacts = output / "producer-direct-artifacts"
    configure_guest(
        cmake=cmake,
        contract_package=contract_package,
        source=source / "producer" / "guest",
        build=direct_build,
        artifact_directory=direct_artifacts,
    )
    build(cmake, direct_build, "product_artifacts")
    verify_artifacts(direct_artifacts, "product")
    verify_identical_artifacts(helper_artifacts, direct_artifacts, "product")

    initial_digest = helper_manifest["source_graph"]["sha256"]
    build(cmake, producer_build, "product_guest")
    rebuilt_manifest = verify_artifacts(helper_artifacts, "product")[1]
    if rebuilt_manifest["source_graph"]["sha256"] != initial_digest:
        raise RuntimeError("source graph digest changed across an incremental rebuild")

    run(cmake, "--install", str(producer_build))
    verify_relocatable_package(producer_install, (source, producer_build))
    relocated = output / "product-relocated"
    shutil.copytree(producer_install, relocated)
    shutil.rmtree(producer_install)
    product_package = relocated / "lib" / "cmake" / "ProductProtocol"
    values_package = relocated / "lib" / "cmake" / "ProductValues"
    if not (product_package / "ProductProtocolContract.cmake").is_file():
        raise RuntimeError("installed protocol package has no guest source materialization")
    if not (values_package / "ProductValuesContract.cmake").is_file():
        raise RuntimeError("installed values package has no guest source materialization")
    targets_file = product_package / "ProductProtocolTargets.cmake"
    if "FILE_SET \"forge_contract_modules\"" not in targets_file.read_text(encoding="utf-8"):
        raise RuntimeError("installed protocol package has no relocatable host module sources")

    alternate_build = output / "alternate-layout-build"
    alternate_install = output / "alternate-layout-install"
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source / "producer",
        build=alternate_build,
        definitions=(
            f"-DCMAKE_INSTALL_PREFIX={alternate_install}",
            "-DPRODUCT_PROTOCOL_DATA_DESTINATION=share/product-layout-alternate",
        ),
    )
    build(cmake, alternate_build, "product_protocol_host_tests")
    run(cmake, "--install", str(alternate_build))
    alternate_product_package = (
        alternate_install / "lib" / "cmake" / "ProductProtocol"
    )

    verify_guest_package_diamond(
        cmake=cmake,
        contract_package=contract_package,
        product_prefixes=(relocated, alternate_install),
        product_package=alternate_product_package,
        values_package=values_package,
        output=output / "package-diamond",
    )
    verify_native_package_diamond(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_prefixes=(relocated, alternate_install),
        product_package=alternate_product_package,
        values_package=values_package,
        output=output / "native-package-diamond",
    )
    verify_keyword_like_descriptor_values(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        output=output / "keyword-descriptor-values",
    )
    verify_conflicting_package_descriptor(
        cmake=cmake,
        contract_package=contract_package,
        product_prefix=relocated,
        values_package=values_package,
        output=output / "conflicting-package-descriptor",
    )
    verify_conflicting_package_materialization(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_prefix=relocated,
        values_package=values_package,
        output=output / "conflicting-package-materialization",
    )
    check_imported_target_seal(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        product_package=product_package,
        output=output / "imported-target-seal",
    )

    consumer_build = output / "consumer-host"
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source / "consumer",
        build=consumer_build,
        prefixes=(relocated,),
        definitions=(f"-DProductProtocol_DIR={product_package}",),
    )
    build(cmake, consumer_build, "product_protocol_consumer")
    run(str(consumer_build / "product_protocol_consumer"))
    build(cmake, consumer_build, "consumer_guest")
    verify_artifacts(consumer_build / "consumer.guest" / "artifacts", "consumer")

    vm_build = output / "vm-host"
    configure_host(
        cmake=cmake,
        cxx_compiler=cxx_compiler,
        forge_package=forge_package,
        contract_package=contract_package,
        source=source / "vm",
        build=vm_build,
        prefixes=(relocated,),
        definitions=(
            f"-DProductProtocol_DIR={product_package}",
            f"-DPRODUCT_PROTOCOL_WASM={helper_artifacts / 'product.wasm'}",
        ),
    )
    build(cmake, vm_build, "product_protocol_vm_tests")
    run(str(vm_build / "product_protocol_vm_tests"))

    check_configure_failures(
        cmake=cmake,
        contract_package=contract_package,
        output=output / "negative",
    )
    check_build_failures(
        cmake=cmake,
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
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    validate(
        cmake=args.cmake,
        cxx_compiler=args.cxx_compiler.absolute(),
        forge_package=args.forge_package.resolve(),
        contract_package=args.contract_package.resolve(),
        source=args.source.resolve(),
        output=args.output.resolve(),
    )


if __name__ == "__main__":
    main()

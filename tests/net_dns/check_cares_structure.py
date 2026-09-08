#!/usr/bin/env python3

import argparse
import hashlib
import re
from pathlib import Path


ARCHIVE_URL = "https://github.com/c-ares/c-ares/releases/download/v1.34.8/c-ares-1.34.8.tar.gz"
ARCHIVE_SHA256 = "c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78"
TREE_MANIFEST_SHA256 = "cc85467961f47f77e6867ff8359786262bde511df45380bbfcb48806ca331988"
UPSTREAM_FILE_COUNT = 532
TREE_ENTRY = re.compile(r"^([0-9a-f]{64})  (.+)$")
IMPLEMENTATION_CLASS = re.compile(r"\b(?:class|struct)\s+[A-Za-z_]")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_tree_manifest(path: Path, errors: list[str]) -> dict[str, str]:
    if not path.is_file():
        errors.append("missing c-ares complete-tree manifest: vendor/c-ares/TREE.SHA256")
        return {}
    if sha256(path) != TREE_MANIFEST_SHA256:
        errors.append("c-ares TREE.SHA256 does not match the pinned complete-tree manifest hash")

    expected: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        match = TREE_ENTRY.fullmatch(line)
        if match is None:
            errors.append(f"invalid c-ares tree manifest entry at line {line_number}")
            continue
        digest, relative = match.groups()
        relative_path = Path(relative)
        if relative_path.is_absolute() or ".." in relative_path.parts or relative in expected:
            errors.append(f"invalid or duplicate c-ares tree manifest path at line {line_number}: {relative}")
            continue
        expected[relative] = digest

    if len(expected) != UPSTREAM_FILE_COUNT:
        errors.append(f"c-ares tree manifest has {len(expected)} entries; expected {UPSTREAM_FILE_COUNT}")
    return expected


def verify_complete_tree(upstream: Path, tree_manifest: Path, errors: list[str]) -> None:
    expected = read_tree_manifest(tree_manifest, errors)
    if not upstream.is_dir():
        errors.append("missing vendored c-ares upstream tree")
        return

    forge_metadata = {"MANIFEST.md", "TREE.SHA256"}
    actual = {
        path.relative_to(upstream).as_posix(): sha256(path)
        for path in upstream.rglob("*")
        if path.is_file() and path.relative_to(upstream).as_posix() not in forge_metadata
    }
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    changed = sorted(relative for relative in set(expected) & set(actual) if expected[relative] != actual[relative])
    if missing:
        errors.append("vendored c-ares tree is missing files: " + ", ".join(missing))
    if extra:
        errors.append("vendored c-ares tree has unexpected files: " + ", ".join(extra))
    if changed:
        errors.append("vendored c-ares tree has changed files: " + ", ".join(changed))


def require_terms(content: str, terms: tuple[str, ...], description: str, errors: list[str]) -> None:
    for term in terms:
        if term not in content:
            errors.append(f"{description} omits required term: {term}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    errors: list[str] = []

    vendor = source / "vendor" / "c-ares"
    manifest_path = vendor / "MANIFEST.md"
    manifest = manifest_path.read_text(encoding="utf-8") if manifest_path.is_file() else ""
    require_terms(manifest, (ARCHIVE_URL, ARCHIVE_SHA256, TREE_MANIFEST_SHA256, "532 upstream files"),
                  "c-ares manifest", errors)
    verify_complete_tree(vendor, vendor / "TREE.SHA256", errors)

    license_path = vendor / "LICENSE.md"
    if not license_path.is_file() or "MIT License" not in license_path.read_text(encoding="utf-8"):
        errors.append("c-ares MIT license is missing or incomplete")
    if not (vendor / "AUTHORS").is_file():
        errors.append("c-ares upstream AUTHORS file is missing")

    provenance = (source / "PROVENANCE.md").read_text(encoding="utf-8")
    require_terms(provenance, ("vendor/c-ares", "1.34.8", "532", "MIT"), "PROVENANCE.md", errors)
    licenses = (source / "THIRD_PARTY_LICENSES").read_text(encoding="utf-8")
    require_terms(licenses, ("c-ares", "1.34.8", "MIT License"), "THIRD_PARTY_LICENSES", errors)

    root_cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    require_terms(
        root_cmake,
        (
            "cmake_policy(SET CMP0077 NEW)",
            "set(CARES_STATIC ON CACHE BOOL",
            "set(CARES_SHARED OFF CACHE BOOL",
            "set(CARES_INSTALL OFF CACHE BOOL",
            "set(CARES_BUILD_TESTS OFF CACHE BOOL",
            "set(CARES_BUILD_TOOLS OFF CACHE BOOL",
            "FORCE)",
            "add_subdirectory(vendor/c-ares EXCLUDE_FROM_ALL)",
            "set_target_properties(\n      c-ares PROPERTIES",
            'INTERFACE_INCLUDE_DIRECTORIES ""',
        ),
        "private c-ares CMake registration",
        errors,
    )
    if "find_package(Cares" in root_cmake or "find_package(c-ares" in root_cmake:
        errors.append("c-ares registration must not discover a system package")

    dns_library = source / "libraries" / "net" / "dns"
    library_cmake = (dns_library / "CMakeLists.txt").read_text(encoding="utf-8")
    require_terms(
        library_cmake,
        (
            "cares_library.cpp",
            "nameserver_formatter.cpp",
            "resolver_socket_registry.cpp",
            "resolver_socket_watch.cpp",
            "resolver_query.cpp",
            "resolver_impl.cpp",
            "details/cares_library.hxx",
            "details/nameserver_formatter.hxx",
            "details/resolver_socket_registry.hxx",
            "details/resolver_socket_watch.hxx",
            "details/resolver_query.hxx",
            "details/resolver_impl.hxx",
            "PRIVATE\n      c-ares",
        ),
        "forge_net_dns CMake registration",
        errors,
    )
    required_pairs = (
        (dns_library / "include" / "forge" / "net" / "dns" / "resolver.cppm", dns_library / "resolver.cpp"),
        (dns_library / "details" / "cares_library.hxx", dns_library / "cares_library.cpp"),
        (dns_library / "details" / "nameserver_formatter.hxx", dns_library / "nameserver_formatter.cpp"),
        (dns_library / "details" / "resolver_socket_registry.hxx", dns_library / "resolver_socket_registry.cpp"),
        (dns_library / "details" / "resolver_socket_watch.hxx", dns_library / "resolver_socket_watch.cpp"),
        (dns_library / "details" / "resolver_query.hxx", dns_library / "resolver_query.cpp"),
        (dns_library / "details" / "resolver_impl.hxx", dns_library / "resolver_impl.cpp"),
    )
    for header, implementation in required_pairs:
        if not header.is_file() or not implementation.is_file():
            errors.append(f"missing create-library pair: {header.name} / {implementation.name}")

    resolver_impl = (dns_library / "resolver_impl.cpp").read_text(encoding="utf-8")
    if IMPLEMENTATION_CLASS.search(resolver_impl):
        errors.append("resolver_impl.cpp must not define implementation classes")
    resolver_query = (dns_library / "resolver_query.cpp").read_text(encoding="utf-8")
    require_terms(
        resolver_query,
        (
            "ARES_OPT_SOCK_STATE_CB",
            "ares_process_fds",
            "ares_timeout",
            "ARES_ETIMEOUT",
            "callback_contexts.emplace",
            "finish_submission_failure",
            "callback_contexts.find(context)",
            "ares_dns_rr_get_name",
            "terminal_owners",
            "callback_executor",
            "is_hard_terminal_failure",
            "preflight_cname_limits",
            "socket_registry.contains",
        ),
        "resolver query ownership implementation",
        errors,
    )
    if "ARES_OPT_EVENT_THREAD" in resolver_query:
        errors.append("resolver query must not enable c-ares event-thread mode")

    registry = (dns_library / "resolver_socket_registry.cpp").read_text(encoding="utf-8")
    require_terms(
        registry,
        (
            "create_or_find",
            "find",
            "remove",
            "contains",
            "snapshot",
            "release_all",
            "generation()",
            "release_borrowed()",
        ),
        "resolver socket registry ownership implementation",
        errors,
    )

    socket_watch = (dns_library / "resolver_socket_watch.cpp").read_text(encoding="utf-8")
    require_terms(
        socket_watch,
        (
            "#include <winsock2.h>",
            "generic::stream_protocol",
            "generic::datagram_protocol",
            "cancel(ignored)",
            "release(ignored)",
        ),
        "resolver socket watch borrowed-handle implementation",
        errors,
    )
    formatter = (dns_library / "nameserver_formatter.cpp").read_text(encoding="utf-8")
    require_terms(
        formatter,
        (
            "format_nameserver_csv",
            "out.append(\"]:\")",
            "out.push_back('%')",
        ),
        "nameserver CSV formatter",
        errors,
    )
    resolver_impl = (dns_library / "resolver_impl.cpp").read_text(encoding="utf-8")
    require_terms(
        resolver_impl,
        (
            "reset_cancellation_state(asio::disable_cancellation{})",
            "address.to_v6().is_link_local()",
            "dns IPv6 link-local nameservers require a scope_id",
        ),
        "resolver close and nameserver validation implementation",
        errors,
    )
    total_limit_check = "length > options.max_total_answer_bytes - total_bytes - value.size()"
    total_limit_position = resolver_query.find(total_limit_check)
    insert_position = resolver_query.find("value.insert(value.end(), segment, segment + length)")
    if total_limit_position == -1 or insert_position == -1 or total_limit_position >= insert_position:
        errors.append("TXT total-answer limit must be checked before materializing a segment")
    terminal_owners_position = resolver_query.find("resolver_query::terminal_owners")
    cname_preflight_position = resolver_query.find("preflight_cname_limits(record);", terminal_owners_position)
    cname_materialization_position = resolver_query.find("edges.push_back")
    if (terminal_owners_position == -1 or cname_preflight_position == -1 or cname_materialization_position == -1 or
            cname_preflight_position >= cname_materialization_position):
        errors.append("CNAME limits must be preflighted before edge string materialization")

    public_modules = sorted((dns_library / "include" / "forge" / "net" / "dns").glob("*.cppm"))
    package_consumer = source / "tests" / "package_net_dns_component" / "main.cpp"
    for path in [*public_modules, package_consumer]:
        content = path.read_text(encoding="utf-8") if path.is_file() else ""
        if "ares_" in content or "<ares" in content:
            errors.append(f"public DNS surface leaks c-ares API: {path.relative_to(source)}")

    if errors:
        raise SystemExit("\n".join(errors))
    print("c-ares full-tree provenance and Forge private DNS boundary are intact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

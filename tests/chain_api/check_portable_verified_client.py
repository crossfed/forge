#!/usr/bin/env python3

from pathlib import Path
import re
import sys


def require(text: str, needle: str, path: Path) -> None:
    if needle not in text:
        raise SystemExit(f"{path}: missing portable verified-client surface: {needle}")


def forbid(text: str, needle: str, path: Path) -> None:
    if needle in text:
        raise SystemExit(f"{path}: forbidden portability dependency: {needle}")


def production_sources(*roots: Path) -> tuple[Path, ...]:
    return tuple(sorted(path for root in roots for path in root.rglob("*")
                        if path.suffix in (".cpp", ".cppm", ".hpp", ".hxx")))


def reject_imports(text: str, path: Path) -> None:
    import_patterns = (
        re.compile(r'^\s*#\s*include\s*[<"][^>"]*(?:filesystem|json)[^>"]*[>"]', re.IGNORECASE),
        re.compile(r'^\s*(?:export\s+)?import\s+[^;]*(?:filesystem|json)[^;]*;', re.IGNORECASE),
    )
    for line_number, line in enumerate(text.splitlines(), start=1):
        if any(pattern.search(line) for pattern in import_patterns):
            raise SystemExit(f"{path}:{line_number}: JSON/filesystem import is outside the portable closure")


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    savanna = root / "libraries/chain/savanna"
    api = root / "libraries/chain/api"
    protocol = root / "libraries/chain/protocol"
    kernel_interfaces = (
        "exceptions.cppm", "types.cppm", "policy.cppm", "finality_core.cppm", "qc.cppm", "rank.cppm",
        "validation.cppm", "vote.cppm", "vote_accumulator.cppm", "finalizer_safety.cppm",
    )
    kernel_implementations = (
        "types.cpp", "policy.cpp", "finality_core.cpp", "qc.cpp", "rank.cpp", "validation.cpp",
        "vote.cpp", "vote_accumulator.cpp", "vote_accumulator_impl.cpp", "finalizer_safety.cpp",
        "details/vote_accumulator_impl.hxx",
    )
    sources = (
        *production_sources(savanna),
        *(api / name for name in (
            "include/forge/chain/api/savanna_finality_verifier.cppm",
            "savanna_finality_verifier.cpp",
            "include/forge/chain/api/contract_table_projection_verifier.cppm",
            "contract_table_projection_verifier.cpp",
            "include/forge/chain/api/verified_client_factory.cppm",
            "verified_client_factory.cpp",
        )),
        *(protocol / name for name in (
            "include/forge/chain/protocol/contract_commitment.cppm",
            "contract_commitment.cpp",
        )),
    )
    if not sources:
        raise SystemExit("portable verified-client production closure is empty")
    for path in sources:
        text = path.read_text(encoding="utf-8")
        portable_text = text.replace('"spine.savanna.finality"', "")
        if re.search(r"(?:spine|storlane|chain_plugin|state_api_impl|block_api_impl|chainbase)",
                     portable_text, re.IGNORECASE):
            raise SystemExit(f"{path}: forbidden Spine/product vocabulary in production")
        reject_imports(text, path)

    wire_scheme = savanna / "include/forge/chain/savanna/finality_witness.cppm"
    require(wire_scheme.read_text(encoding="utf-8"), '"spine.savanna.finality"', wire_scheme)

    kernel = (
        *(savanna / "include/forge/chain/savanna" / name for name in kernel_interfaces),
        *(savanna / name for name in kernel_implementations),
    )
    for path in kernel:
        text = path.read_text(encoding="utf-8")
        if re.search(r"forge\.chain\.protocol|forge::chain::protocol|forge/chain/protocol", text):
            raise SystemExit(f"{path}: neutral Savanna kernel imports Chain Protocol")

    policy_state = savanna / "include/forge/chain/savanna/policy_state.cppm"
    require(policy_state.read_text(encoding="utf-8"), "struct finalizer_policy_state", policy_state)
    genesis = savanna / "include/forge/chain/savanna/genesis.cppm"
    genesis_text = genesis.read_text(encoding="utf-8")
    for needle in ("struct genesis",):
        require(genesis_text, needle, genesis)
    for needle in ("filesystem", "load_genesis", "save_genesis"):
        forbid(genesis_text, needle, genesis)

    commitment = protocol / "include/forge/chain/protocol/contract_commitment.cppm"
    commitment_text = commitment.read_text(encoding="utf-8")
    for needle in ("struct contract_table_location", "struct table_value", "struct primary_value",
                   "struct secondary_value", "contract_table_key", "contract_index_prefix",
                   "contract_primary_key", "contract_secondary_key"):
        require(commitment_text, needle, commitment)
    api_cmake = api / "CMakeLists.txt"
    api_cmake_text = api_cmake.read_text(encoding="utf-8")
    require(api_cmake_text, "forge_chain_savanna", api_cmake)
    for cmake in (api_cmake, protocol / "CMakeLists.txt", savanna / "CMakeLists.txt"):
        text = cmake.read_text(encoding="utf-8")
        if re.search(r"(?:spine|storlane)[_:]", text, re.IGNORECASE):
            raise SystemExit(f"{cmake}: portable production closure must not link a product target")
        forbid(text, "forge_codec_json", cmake)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

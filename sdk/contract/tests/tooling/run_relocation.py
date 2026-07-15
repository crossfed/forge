#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import tarfile
import time
from pathlib import Path


def run(*command: str) -> None:
    subprocess.run(command, check=True)


def contains_path(path: Path, needle: bytes) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as stream:
        tail = b""
        while chunk := stream.read(1024 * 1024):
            data = tail + chunk
            if needle in data:
                return True
            tail = data[-len(needle) :] if needle else b""
    return False


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    output = args.output.resolve()
    shutil.rmtree(output, ignore_errors=True)
    unpacked = output / "sdk"
    unpacked.mkdir(parents=True)
    with tarfile.open(args.archive, "r:gz") as archive:
        archive.extractall(unpacked, filter="data")

    roots = [entry for entry in unpacked.iterdir() if entry.is_dir()]
    if len(roots) != 1:
        raise RuntimeError(f"expected one SDK root, found {len(roots)}")
    sdk = roots[0]

    config = sdk / "lib" / "cmake" / "ForgeContract" / "ForgeContractConfig.cmake"
    release = 'set(ForgeContract_PROFILE "release")' in config.read_text()
    candidates = list(sdk.rglob("*"))
    if not release:
        candidates = [path for path in candidates if path.suffix in {".cmake", ".json", ".pc", ".txt"}]
    leaked = [path for path in candidates if contains_path(path, str(args.source_root.resolve()).encode())]
    if leaked:
        raise RuntimeError(f"installed SDK contains source path: {leaked[0]}")

    source = output / "consumer"
    shutil.copytree(sdk / "share" / "forge-contract" / "examples" / "hello", source)
    build = output / "build"
    package = sdk / "lib" / "cmake" / "ForgeContract"
    run(args.cmake, "-S", str(source), "-B", str(build), "-G", "Ninja", f"-DForgeContract_DIR={package}")
    run(args.cmake, "--build", str(build), "-j", "4")

    for suffix in ("wasm", "abi", "contract.json"):
        artifact = build / f"hello.{suffix}"
        if not artifact.is_file() or artifact.stat().st_size == 0:
            raise RuntimeError(f"missing relocated SDK artifact: {artifact}")

    wasm = build / "hello.wasm"
    first_mtime = wasm.stat().st_mtime_ns
    time.sleep(0.01)
    with (source / "hello.cpp").open("a", encoding="utf-8") as stream:
        stream.write("\n")
    run(args.cmake, "--build", str(build), "-j", "4")
    if wasm.stat().st_mtime_ns <= first_mtime:
        raise RuntimeError("contract source change did not rebuild the WebAssembly artifact")


if __name__ == "__main__":
    main()

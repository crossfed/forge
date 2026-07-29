#!/usr/bin/env python3

import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys


def run(command):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"command failed:\n{' '.join(map(str, command))}\n{result.stdout}")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args, root):
    source = root / "source"
    binary = root / "build"
    shutil.copytree(args.source.parent, source)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.31)\n"
        "project(reproducible_contract LANGUAGES NONE)\n"
        "find_package(ForgeContract CONFIG REQUIRED)\n"
        "forge_add_contract(hello SOURCES hello.cpp HEADERS local_value.hpp)\n",
        encoding="utf-8",
    )
    run(
        [
            str(args.cmake),
            "-S",
            str(source),
            "-B",
            str(binary),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DForgeContract_DIR={args.package}",
        ]
    )
    run([str(args.cmake), "--build", str(binary), "--target", "hello", "-j", "4"])
    cache = (binary / "hello.contract" / "CMakeCache.txt").read_text(encoding="utf-8")
    if "CMAKE_BUILD_TYPE:STRING=Release" not in cache:
        raise RuntimeError("forge_add_contract did not propagate the parent Release build type")
    return binary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True, type=pathlib.Path)
    parser.add_argument("--package", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    if args.output.exists():
        shutil.rmtree(args.output)
    first = build(args, args.output / "absolute-path-a")
    second = build(args, args.output / "different" / "absolute-path-b")
    for artifact in ("hello.wasm", "hello.abi", "hello.contract.json"):
        left = first / artifact
        right = second / artifact
        if left.read_bytes() != right.read_bytes():
            raise RuntimeError(
                f"contract artifact depends on its absolute build path: {artifact} "
                f"({digest(left)} != {digest(right)})"
            )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"run_reproducibility: {error}", file=sys.stderr)
        raise

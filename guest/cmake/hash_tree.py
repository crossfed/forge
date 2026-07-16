#!/usr/bin/env python3

import argparse
import hashlib
import pathlib


def hash_tree(root: pathlib.Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*"), key=lambda value: value.as_posix()):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        if path.is_symlink():
            digest.update(b"L\0" + relative + b"\0")
            digest.update(path.readlink().as_posix().encode("utf-8") + b"\0")
        elif path.is_file():
            digest.update(b"F\0" + relative + b"\0")
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            digest.update(b"\0")
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    if not args.root.is_dir():
        raise SystemExit(f"sysroot does not exist: {args.root}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(hash_tree(args.root) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

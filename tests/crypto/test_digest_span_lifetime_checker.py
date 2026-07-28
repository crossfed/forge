#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import subprocess
import sys


def run(checker: pathlib.Path, fixture: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(checker), str(fixture)],
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    checker = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()

    valid = run(checker, fixtures / "valid")
    if valid.returncode != 0:
        print(valid.stderr, file=sys.stderr)
        return 1

    expected_diagnostics = {
        "auto": 1,
        "typed": 1,
        "existing": 1,
        "member": 1,
        "returned": 3,
        "co_return": 1,
        "direct": 1,
        "list": 1,
    }
    for name, expected in expected_diagnostics.items():
        invalid = run(checker, fixtures / name)
        if invalid.returncode == 0:
            print(f"{name}: checker accepted a dangling digest span", file=sys.stderr)
            return 1
        if "span refers to a temporary digest" not in invalid.stderr:
            print(f"{name}: checker returned an unexpected diagnostic:\n{invalid.stderr}", file=sys.stderr)
            return 1
        actual = invalid.stderr.count("span refers to a temporary digest")
        if actual != expected:
            print(
                f"{name}: checker produced {actual} diagnostics instead of {expected}:\n{invalid.stderr}",
                file=sys.stderr,
            )
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

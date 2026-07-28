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
        "auto": ["libraries/value.cpp:2: span refers to a temporary digest"],
        "typed": ["libraries/value.cpp:2: span refers to a temporary digest"],
        "existing": ["libraries/value.cpp:3: span refers to a temporary digest"],
        "member": ["libraries/value.cpp:2: span refers to a temporary digest"],
        "returned": [
            "libraries/value.cpp:2: span refers to a temporary digest",
            "libraries/value.cpp:6: span refers to a temporary digest",
            "libraries/value.cpp:10: span refers to a temporary digest",
        ],
        "co_return": ["libraries/value.cpp:2: span refers to a temporary digest"],
        "direct": ["libraries/value.cpp:2: span refers to a temporary digest"],
        "list": ["libraries/value.cpp:2: span refers to a temporary digest"],
        "wrapped": [
            "libraries/value.cpp:3: span refers to a temporary digest",
            "libraries/value.cpp:4: span refers to a temporary digest",
            "libraries/value.cpp:5: span refers to a temporary digest",
            "libraries/value.cpp:9: span refers to a temporary digest",
            "libraries/value.cpp:13: span refers to a temporary digest",
        ],
        "hxx": [
            "libraries/details/value.hxx:2: span refers to a temporary digest",
            "plugins/details/value.hxx:2: span refers to a temporary digest",
        ],
    }
    for name, expected in expected_diagnostics.items():
        invalid = run(checker, fixtures / name)
        if invalid.returncode == 0:
            print(f"{name}: checker accepted a dangling digest span", file=sys.stderr)
            return 1
        if "span refers to a temporary digest" not in invalid.stderr:
            print(f"{name}: checker returned an unexpected diagnostic:\n{invalid.stderr}", file=sys.stderr)
            return 1
        actual = invalid.stderr.strip().splitlines()
        if actual != expected:
            print(
                f"{name}: checker produced unexpected diagnostics:\n"
                f"expected: {expected}\n"
                f"actual:   {actual}",
                file=sys.stderr,
            )
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

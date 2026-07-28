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

    for name in ("auto", "typed", "existing", "member", "returned"):
        invalid = run(checker, fixtures / name)
        if invalid.returncode == 0:
            print(f"{name}: checker accepted a dangling digest span", file=sys.stderr)
            return 1
        if "span refers to a temporary digest" not in invalid.stderr:
            print(f"{name}: checker returned an unexpected diagnostic:\n{invalid.stderr}", file=sys.stderr)
            return 1
        if name == "returned" and invalid.stderr.count("span refers to a temporary digest") != 3:
            print(f"{name}: checker did not reject every temporary digest return:\n{invalid.stderr}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

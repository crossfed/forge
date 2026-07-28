#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys


STATEMENT = re.compile(r"(?P<body>[^;]+);", re.DOTALL)
ASSIGNMENT = re.compile(r"(?<![=!<>])=(?!=)")
TEMPORARY_SPAN_RHS = re.compile(
    r"^\s*(?:(?![=+*/?!&|]).)*?\b(?:hash|digest)\s*\((?:(?!;).)*?\)\s*"
    r"\.to_(?:uint8_)?span\s*\(\s*\)\s*$",
    re.DOTALL,
)


def find_dangling_assignments(text: str) -> list[int]:
    result: list[int] = []
    for statement in STATEMENT.finditer(text):
        body = statement.group("body")
        for assignment in ASSIGNMENT.finditer(body):
            if TEMPORARY_SPAN_RHS.fullmatch(body[assignment.end() :]):
                result.append(statement.start("body") + assignment.start())
                break
    return result


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    failures: list[str] = []
    for root_name in ("libraries", "plugins"):
        root = source / root_name
        for path in sorted((*root.rglob("*.cpp"), *root.rglob("*.cppm"))):
            text = path.read_text(encoding="utf-8")
            for offset in find_dangling_assignments(text):
                line = text.count("\n", 0, offset) + 1
                failures.append(f"{path.relative_to(source)}:{line}: span refers to a temporary digest")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

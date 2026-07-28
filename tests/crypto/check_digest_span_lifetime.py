#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys


ASSIGN_TEMPORARY_SPAN = re.compile(
    r"(?:const\s+)?auto\s+\w+\s*=\s*"
    r"(?:(?!;).)*?(?:hash|digest)\s*\((?:(?!;).)*?\)\s*"
    r"\.to_(?:uint8_)?span\s*\(\s*\)\s*;",
    re.DOTALL,
)


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    failures: list[str] = []
    for root_name in ("libraries", "plugins"):
        root = source / root_name
        for path in sorted((*root.rglob("*.cpp"), *root.rglob("*.cppm"))):
            text = path.read_text(encoding="utf-8")
            for match in ASSIGN_TEMPORARY_SPAN.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{path.relative_to(source)}:{line}: span refers to a temporary digest")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

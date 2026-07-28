#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys


STATEMENT = re.compile(r"(?P<body>[^;]+);", re.DOTALL)
ASSIGNMENT = re.compile(r"(?<![=!<>])=(?!=)")
RETURN_EXPRESSION = re.compile(r"\b(?:co_return|return)\s+(?P<expression>.+)\s*$", re.DOTALL)
DIRECT_INITIALIZATION_PREFIX = re.compile(
    r"(?:^|[{}])\s*"
    r"(?P<type>"
    r"(?:(?:const|volatile)\s+)*"
    r"(?:auto|(?:(?:[A-Za-z_]\w*)::)*span(?:\s*<[^;{}]+>)?)"
    r"(?:\s+(?:const|volatile))*"
    r"(?:\s*&&?)?"
    r")"
    r"\s+(?P<name>[A-Za-z_]\w*)\s*"
    r"(?P<open>[\(\{])",
    re.DOTALL,
)
TEMPORARY_SPAN_EXPRESSION = re.compile(
    r"^\s*(?:(?![=+*/?!&|]).)*?\b(?:hash|digest)\s*\((?:(?!;).)*?\)\s*"
    r"\.to_(?:uint8_)?span\s*\(\s*\)\s*$",
    re.DOTALL,
)
EXPLICIT_SPAN_CONSTRUCTION = re.compile(
    r"^\s*std::span(?:\s*<[^;{}]+>)?\s*"
    r"(?P<open>[\(\{])\s*(?P<expression>.+)\s*(?P<close>[\)\}])\s*$",
    re.DOTALL,
)
SOURCE_PATTERNS = ("*.cpp", "*.cppm", "*.hxx")
EXCLUDED_DIRECTORIES = {"build", "donor", "donors", "vendor"}


def is_live_source(path: pathlib.Path, root: pathlib.Path) -> bool:
    directories = path.relative_to(root).parts[:-1]
    return not any(
        directory in EXCLUDED_DIRECTORIES
        or directory.startswith("build-")
        or directory.startswith("cmake-build-")
        for directory in directories
    )


def find_closing_delimiter(text: str, start: int) -> int | None:
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = [text[start]]
    for offset in range(start + 1, len(text)):
        character = text[offset]
        if character in pairs:
            stack.append(character)
        elif character in pairs.values():
            if not stack or pairs[stack[-1]] != character:
                return None
            stack.pop()
            if not stack:
                return offset
    return None


def unwrap_grouping_parentheses(expression: str) -> str:
    unwrapped = expression.strip()
    while unwrapped.startswith("("):
        close_offset = find_closing_delimiter(unwrapped, 0)
        if close_offset != len(unwrapped) - 1:
            break
        unwrapped = unwrapped[1:close_offset].strip()
    return unwrapped


def is_temporary_digest_span(expression: str) -> bool:
    expression = unwrap_grouping_parentheses(expression)
    if TEMPORARY_SPAN_EXPRESSION.fullmatch(expression):
        return True

    constructed = EXPLICIT_SPAN_CONSTRUCTION.fullmatch(expression)
    if not constructed:
        return False
    if (constructed.group("open"), constructed.group("close")) not in (("(", ")"), ("{", "}")):
        return False
    return is_temporary_digest_span(constructed.group("expression"))


def direct_initializations(body: str) -> list[tuple[re.Match[str], str]]:
    result: list[tuple[re.Match[str], str]] = []
    for initialized in DIRECT_INITIALIZATION_PREFIX.finditer(body):
        open_offset = initialized.start("open")
        close_offset = find_closing_delimiter(body, open_offset)
        if close_offset is None or body[close_offset + 1 :].strip():
            continue
        result.append((initialized, body[open_offset + 1 : close_offset]))
    return result


def find_dangling_spans(text: str) -> list[int]:
    result: list[int] = []
    for statement in STATEMENT.finditer(text):
        body = statement.group("body")
        for assignment in ASSIGNMENT.finditer(body):
            if is_temporary_digest_span(body[assignment.end() :]):
                result.append(statement.start("body") + assignment.start())
                break
        returned = RETURN_EXPRESSION.search(body)
        if returned and is_temporary_digest_span(returned.group("expression")):
            result.append(statement.start("body") + returned.start())
        for initialized, expression in direct_initializations(body):
            if is_temporary_digest_span(expression):
                result.append(statement.start("body") + initialized.start("type"))
                break
    return result


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    failures: list[str] = []
    for root_name in ("libraries", "plugins"):
        root = source / root_name
        paths = (path for pattern in SOURCE_PATTERNS for path in root.rglob(pattern))
        for path in sorted(path for path in paths if is_live_source(path, root)):
            text = path.read_text(encoding="utf-8")
            for offset in find_dangling_spans(text):
                line = text.count("\n", 0, offset) + 1
                failures.append(f"{path.relative_to(source)}:{line}: span refers to a temporary digest")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

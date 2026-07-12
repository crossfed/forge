#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path


SOURCE_SUFFIXES = {".cpp", ".cppm", ".hpp", ".hxx"}
LAYOUT_ROOTS = ("libraries", "plugins")
SCAN_ROOTS = ("libraries", "plugins", "tests")
EXCLUDED_PARTS = {".git", "legacy", "vendor", "__pycache__"}
MODULE_DECLARATION = re.compile(r"^\s*export\s+module\s+(forge(?:\.[A-Za-z_][A-Za-z0-9_]*)+)\s*;")
MODULE_IMPORT = re.compile(r"^\s*(?:export\s+)?import\s+(forge(?:\.[A-Za-z_][A-Za-z0-9_]*)+)\s*;")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"][^>"]+[>"])')
BROAD_EXPORT = re.compile(r"^\s*export\s*\{")
CONDITIONAL_START = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
CONDITIONAL_BRANCH = re.compile(r"^\s*#\s*(?:elif|else)\b")
CONDITIONAL_END = re.compile(r"^\s*#\s*endif\b")


def source_files(root: Path, roots: tuple[str, ...]) -> list[Path]:
   files: list[Path] = []
   for name in roots:
      base = root / name
      if not base.exists():
         continue
      for path in base.rglob("*"):
         if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
         relative = path.relative_to(root)
         if any(part in EXCLUDED_PARTS or part.startswith("build-") for part in relative.parts):
            continue
         files.append(path)
   return sorted(files)


def check_layout(root: Path, errors: list[str]) -> None:
   for path in source_files(root, LAYOUT_ROOTS):
      relative = path.relative_to(root)
      parts = relative.parts
      if path.suffix == ".hxx" and "details" not in parts:
         errors.append(f"{relative}: private .hxx must live under details/")
      if path.suffix == ".hpp" and "details" in parts:
         errors.append(f"{relative}: details/ headers must use .hxx")
      if path.suffix == ".cppm" and "include" not in parts:
         errors.append(f"{relative}: public .cppm must live under include/")
      if path.suffix == ".cpp" and ("include" in parts or "details" in parts):
         errors.append(f"{relative}: implementation .cpp must live at the library/plugin root")


def check_aggregates(root: Path, errors: list[str]) -> None:
   plugin_source = root / "plugins" / "plugins.cpp"
   if plugin_source.exists():
      errors.append("plugins/plugins.cpp: code-less plugin aggregate must not own a source")

   cmake = (root / "plugins" / "CMakeLists.txt").read_text()
   if not re.search(r"add_library\s*\(\s*forge_plugins\s+INTERFACE\s*\)", cmake):
      errors.append("plugins/CMakeLists.txt: forge_plugins must be an INTERFACE target")

   anchor = re.compile(r"\b(?:aggregate|dummy)_anchor\b")
   for path in source_files(root, ("libraries", "plugins", "tests")):
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         if anchor.search(line):
            errors.append(f"{path.relative_to(root)}:{line_number}: dummy anchor symbol is forbidden")


def check_modules(root: Path, files: list[Path], errors: list[str]) -> None:
   declarations: dict[str, list[tuple[Path, int]]] = defaultdict(list)
   imports: list[tuple[str, Path, int]] = []

   for path in files:
      relative = path.relative_to(root)
      seen_imports: dict[str, int] = {}
      seen_includes: dict[tuple[str, tuple[tuple[int, int], ...]], int] = {}
      conditional_stack: list[list[int]] = []
      next_conditional = 0

      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         if CONDITIONAL_START.match(line):
            next_conditional += 1
            conditional_stack.append([next_conditional, 0])
         elif CONDITIONAL_BRANCH.match(line) and conditional_stack:
            conditional_stack[-1][1] += 1
         elif CONDITIONAL_END.match(line) and conditional_stack:
            conditional_stack.pop()

         declaration = MODULE_DECLARATION.match(line)
         if declaration:
            declarations[declaration.group(1)].append((relative, line_number))

         imported = MODULE_IMPORT.match(line)
         if imported:
            name = imported.group(1)
            imports.append((name, relative, line_number))
            if name in seen_imports:
               errors.append(
                  f"{relative}:{line_number}: duplicate import {name} "
                  f"(first at line {seen_imports[name]})"
               )
            else:
               seen_imports[name] = line_number

         included = INCLUDE.match(line)
         if included:
            context = tuple((block, branch) for block, branch in conditional_stack)
            key = (included.group(1), context)
            if key in seen_includes:
               errors.append(
                  f"{relative}:{line_number}: duplicate include {included.group(1)} "
                  f"in the same conditional branch (first at line {seen_includes[key]})"
               )
            else:
               seen_includes[key] = line_number

         if path.suffix == ".cppm" and BROAD_EXPORT.match(line):
            errors.append(f"{relative}:{line_number}: manual broad export block is forbidden")

   for name, owners in sorted(declarations.items()):
      if len(owners) > 1:
         locations = ", ".join(f"{path}:{line}" for path, line in owners)
         errors.append(f"module {name} has multiple declarations: {locations}")

   known_modules = set(declarations)
   for name, path, line_number in imports:
      if name not in known_modules:
         errors.append(f"{path}:{line_number}: import references unknown Forge module {name}")


def main() -> int:
   if len(sys.argv) != 2:
      print("usage: check_structure.py <repository-root>", file=sys.stderr)
      return 2

   root = Path(sys.argv[1]).resolve()
   errors: list[str] = []
   files = source_files(root, SCAN_ROOTS)

   check_layout(root, errors)
   check_aggregates(root, errors)
   check_modules(root, files, errors)

   if errors:
      for error in sorted(set(errors)):
         print(error, file=sys.stderr)
      return 1

   print(f"Forge structure check passed ({len(files)} first-party source files scanned)")
   return 0


if __name__ == "__main__":
   raise SystemExit(main())

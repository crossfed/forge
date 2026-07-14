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
MODULE_NAME = r"forge(?:\.[A-Za-z_][A-Za-z0-9_]*)+(?::[A-Za-z_][A-Za-z0-9_]*)?"
MODULE_DECLARATION = re.compile(rf"^\s*export\s+module\s+({MODULE_NAME})\s*;")
MODULE_UNIT = re.compile(rf"^\s*(?:export\s+)?module\s+({MODULE_NAME})\s*;")
MODULE_IMPORT = re.compile(rf"^\s*(?:export\s+)?import\s+({MODULE_NAME}|:[A-Za-z_][A-Za-z0-9_]*)\s*;")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"][^>"]+[>"])')
BROAD_EXPORT = re.compile(r"^\s*export\s*\{")
CONDITIONAL_START = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
CONDITIONAL_BRANCH = re.compile(r"^\s*#\s*(?:elif|else)\b")
CONDITIONAL_END = re.compile(r"^\s*#\s*endif\b")
PRIVATE_DECLARATION = re.compile(r"^(?:class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_:]*)")
VM_WASM_EXPORT = re.compile(r"\bFORGE_VM_WASM_EXPORT\b")


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


def component_roots(root: Path) -> list[Path]:
   roots: list[Path] = []
   for top in LAYOUT_ROOTS:
      for cmake in (root / top).rglob("CMakeLists.txt"):
         component = cmake.parent
         if any(component.glob("*.cpp")):
            roots.append(component)
   return sorted(roots)


def matching_headers(component: Path, stem: str) -> list[Path]:
   matches: list[Path] = []
   include = component / "include"
   if include.exists():
      matches.extend(include.rglob(f"{stem}.cppm"))
   private = component / "details" / f"{stem}.hxx"
   if private.exists():
      matches.append(private)
   return sorted(matches)


def check_pairing(root: Path, errors: list[str]) -> None:
   for component in component_roots(root):
      sources = {path.stem: path for path in component.glob("*.cpp")}
      headers = {stem: matching_headers(component, stem) for stem in sources}

      for stem, source in sorted(sources.items()):
         relative = source.relative_to(root)
         direct = headers[stem]
         if len(direct) == 1:
            continue
         if len(direct) > 1:
            owners = ", ".join(str(path.relative_to(root)) for path in direct)
            errors.append(f"{relative}: implementation has multiple exact owners: {owners}")
            continue

         aspect_owners = [
            owner
            for owner in sources
            if stem.startswith(f"{owner}_") and len(headers[owner]) == 1
         ]
         if aspect_owners:
            continue
         errors.append(
            f"{relative}: implementation needs an exact {stem}.cppm/{stem}.hxx owner "
            "or a paired X.cpp for an X_<aspect>.cpp source"
         )


def check_macro_only_header(root: Path, path: Path, errors: list[str]) -> None:
   text = re.sub(r"/\*.*?\*/", "", path.read_text(errors="ignore"), flags=re.DOTALL)
   in_macro = False

   for line_number, line in enumerate(text.splitlines(), 1):
      stripped = re.sub(r"//.*$", "", line).strip()
      if in_macro:
         in_macro = line.rstrip().endswith("\\")
         continue
      if not stripped:
         continue
      if stripped.startswith("#"):
         if re.match(r"#\s*define\b", stripped):
            in_macro = line.rstrip().endswith("\\")
         continue
      errors.append(
         f"{path.relative_to(root)}:{line_number}: macro-only public header contains a C++ declaration"
      )


def check_vm_wasm_boundaries(root: Path, errors: list[str]) -> None:
   component = root / "libraries" / "vm" / "wasm"
   if not component.exists():
      return

   details = component / "details"
   if details.exists():
      errors.append(f"{details.relative_to(root)}: vm_wasm must not install or compile private source headers")

   include = component / "include" / "forge" / "vm" / "wasm"
   allowed_headers = {"host_function.hpp", "opcode_macros.hpp"}
   headers = {path.name for path in include.glob("*.hpp")}
   unexpected = headers - allowed_headers
   if unexpected:
      errors.append(f"{include.relative_to(root)}: unexpected public headers: {', '.join(sorted(unexpected))}")

   for name in sorted(allowed_headers):
      path = include / name
      if not path.exists():
         errors.append(f"{path.relative_to(root)}: required macro-only public header is missing")
         continue
      check_macro_only_header(root, path, errors)

   for path in sorted(include.glob("*.cppm")):
      relative = path.relative_to(root)
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         included = INCLUDE.match(line)
         if included and (".hxx" in included.group(1) or "details/" in included.group(1)):
            errors.append(f"{relative}:{line_number}: public VM module includes a private source header")
         if included and "forge/vm/wasm/" in included.group(1) and included.group(1) not in {
            "<forge/vm/wasm/host_function.hpp>",
            "<forge/vm/wasm/opcode_macros.hpp>",
         }:
            errors.append(f"{relative}:{line_number}: VM components must use module imports")
         if VM_WASM_EXPORT.search(line):
            errors.append(f"{relative}:{line_number}: FORGE_VM_WASM_EXPORT is forbidden")


def check_plugin_impl_ownership(root: Path, errors: list[str]) -> None:
   for path in sorted((root / "plugins").rglob("details/plugin_impl.hxx")):
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         declaration = PRIVATE_DECLARATION.match(line)
         if declaration and declaration.group(1) != "plugin::impl":
            errors.append(
               f"{path.relative_to(root)}:{line_number}: plugin_impl.hxx may only own plugin::impl; "
               f"move {declaration.group(1)} to its exact private header"
            )


def check_modules(root: Path, files: list[Path], errors: list[str]) -> None:
   declarations: dict[str, list[tuple[Path, int]]] = defaultdict(list)
   imports: list[tuple[str, Path, int]] = []

   for path in files:
      relative = path.relative_to(root)
      source_lines = path.read_text(errors="ignore").splitlines()
      unit_name = next((match.group(1) for line in source_lines if (match := MODULE_UNIT.match(line))), None)
      unit_primary = unit_name.split(":", 1)[0] if unit_name else None
      seen_imports: dict[str, int] = {}
      seen_includes: dict[tuple[str, tuple[tuple[int, int], ...]], int] = {}
      conditional_stack: list[list[int]] = []
      next_conditional = 0

      for line_number, line in enumerate(source_lines, 1):
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
            if name.startswith(":"):
               if unit_primary is None:
                  errors.append(f"{relative}:{line_number}: relative import has no owning module")
                  continue
               name = f"{unit_primary}{name}"
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
   check_pairing(root, errors)
   check_vm_wasm_boundaries(root, errors)
   check_plugin_impl_ownership(root, errors)
   check_modules(root, files, errors)

   if errors:
      for error in sorted(set(errors)):
         print(error, file=sys.stderr)
      return 1

   print(f"Forge structure check passed ({len(files)} first-party source files scanned)")
   return 0


if __name__ == "__main__":
   raise SystemExit(main())

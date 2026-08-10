#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys


def main() -> int:
   root = pathlib.Path(sys.argv[1])
   library = root / "libraries" / "cli"
   errors: list[str] = []

   public = sorted((library / "include" / "forge" / "cli").glob("*.cppm"))
   expected = {
      "command.cppm",
      "completion.cppm",
      "exceptions.cppm",
      "parser.cppm",
      "runner.cppm",
      "terminal.cppm",
   }
   actual = {path.name for path in public}
   if actual != expected:
      errors.append(f"public CLI modules are {sorted(actual)!r}, expected {sorted(expected)!r}")

   forbidden = re.compile(
      r"(?:#\s*include\s*[<\"](?:CLI/|boost/charconv/)|\bCLI::|\bboost::charconv::|\bboost::program_options\b)"
   )
   for path in public:
      source = path.read_text()
      if forbidden.search(source):
         errors.append(f"{path.relative_to(root)} leaks parser backend types through the public module")
      expected_declaration = f"export module forge.cli.{path.stem};"
      if expected_declaration not in source:
         errors.append(f"{path.relative_to(root)} must declare {expected_declaration}")

   for path in sorted(library.glob("*.cpp")):
      source = path.read_text()
      if forbidden.search(source) and path.name != "parser.cpp":
         errors.append(f"{path.relative_to(root)} uses CLI11 outside parser.cpp")

   version = (root / "vendor" / "CLI11" / "include" / "CLI" / "Version.hpp").read_text()
   if '#define CLI11_VERSION "2.6.2"' not in version:
      errors.append("vendored CLI11 version is not 2.6.2")

   production_roots = [
      root / "CMakeLists.txt",
      root / "libraries",
      root / "plugins",
      root / "programs",
      root / "tools",
   ]
   for production_root in production_roots:
      paths = [production_root] if production_root.is_file() else production_root.rglob("*")
      for path in paths:
         if not path.is_file() or path.suffix not in {"", ".cpp", ".cppm", ".hpp", ".hxx", ".txt"}:
            continue
         if "forge_chain_client" in path.read_text(errors="ignore"):
            errors.append(f"{path.relative_to(root)} creates the forbidden duplicate forge_chain_client layer")

   if errors:
      for error in errors:
         print(error, file=sys.stderr)
      return 1
   print("forge_cli public modules are backend-neutral; CLI11 is pinned to 2.6.2")
   return 0


if __name__ == "__main__":
   raise SystemExit(main())

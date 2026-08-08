#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import shutil
import subprocess


def command_arguments(entry: dict[str, object]) -> list[str]:
   arguments = entry.get("arguments")
   if isinstance(arguments, list):
      return [str(argument) for argument in arguments]
   return shlex.split(str(entry["command"]))


def optimization_flags(arguments: list[str]) -> list[str]:
   exact = {"-O0", "-O1", "-O2", "-O3", "-Os", "-Oz", "-Og", "-Ofast", "/Od", "/O1", "/O2", "/Ox"}
   return [argument for argument in arguments if argument in exact]


def sanitizer_flags(arguments: list[str]) -> list[str]:
   return [argument for argument in arguments if "sanitize=" in argument]


def main() -> int:
   parser = argparse.ArgumentParser()
   parser.add_argument("--cmake", required=True)
   parser.add_argument("--source", required=True)
   parser.add_argument("--binary", required=True)
   parser.add_argument("--forge-source", required=True)
   parser.add_argument("--generator", required=True)
   parser.add_argument("--multi-config", action="store_true")
   parser.add_argument("--c-compiler", required=True)
   parser.add_argument("--cxx-compiler", required=True)
   args = parser.parse_args()

   binary = pathlib.Path(args.binary)
   shutil.rmtree(binary, ignore_errors=True)
   configuration_option = (
      "-DCMAKE_CONFIGURATION_TYPES=Debug" if args.multi_config else "-DCMAKE_BUILD_TYPE=Debug"
   )
   subprocess.run(
      [
         args.cmake,
         "-S",
         args.source,
         "-B",
         str(binary),
         "-G",
         args.generator,
         configuration_option,
         f"-DCMAKE_C_COMPILER={args.c_compiler}",
         f"-DCMAKE_CXX_COMPILER={args.cxx_compiler}",
         f"-DFORGE_SOURCE_DIR={args.forge_source}",
      ],
      check=True,
   )

   actual_multi_config = (binary / "generator-is-multi-config.txt").read_text().strip() == "true"
   if actual_multi_config != args.multi_config:
      raise RuntimeError(
         f"generator {args.generator!r}: multi-config={actual_multi_config}, expected {args.multi_config}"
      )

   database = json.loads((binary / "compile_commands.json").read_text())
   by_name = {pathlib.Path(str(entry["file"])).name: command_arguments(entry) for entry in database}
   msvc_style = "/Od" in by_name["forge_owned.cpp"]
   expected_optimized = "/O2" if msvc_style else "-O2"
   expected_debug = "/Od" if msvc_style else "-O0"
   expected_sanitizer = "/fsanitize=address" if msvc_style else "-fsanitize=address,undefined"
   expected_suppression = None if msvc_style else "-fno-sanitize=alignment"

   for source in ("vendor_c.c", "vendor_cxx.cpp"):
      flags = optimization_flags(by_name[source])
      if not flags or flags[-1] != expected_optimized:
         raise RuntimeError(f"{source}: effective optimization is {flags!r}, expected final {expected_optimized}")
      if expected_debug not in flags:
         raise RuntimeError(f"{source}: fixture did not exercise parent Debug optimization: {flags!r}")
      if expected_sanitizer not in by_name[source]:
         raise RuntimeError(f"{source}: sanitizer option was not preserved")
      if any(flag.startswith("-fno-sanitize=") for flag in sanitizer_flags(by_name[source])):
         raise RuntimeError(f"{source}: unexpected sanitizer suppression")

   for source in ("vendor_alignment.c", "vendor_alignment.cpp"):
      flags = optimization_flags(by_name[source])
      if not flags or flags[-1] != expected_optimized:
         raise RuntimeError(f"{source}: effective optimization is {flags!r}, expected final {expected_optimized}")
      if expected_debug not in flags:
         raise RuntimeError(f"{source}: fixture did not exercise parent Debug optimization: {flags!r}")
      sanitizers = sanitizer_flags(by_name[source])
      if expected_sanitizer not in sanitizers:
         raise RuntimeError(f"{source}: parent sanitizer option was not preserved: {sanitizers!r}")
      if expected_suppression is None:
         if any(flag.startswith("-fno-sanitize=") for flag in sanitizers):
            raise RuntimeError(f"{source}: MSVC-style compilation unexpectedly disables a sanitizer")
      elif sanitizers[-2:] != [expected_sanitizer, expected_suppression]:
         raise RuntimeError(
            f"{source}: sanitizer flags are {sanitizers!r}, expected final {expected_sanitizer!r}, {expected_suppression!r}"
         )

   owned_flags = optimization_flags(by_name["forge_owned.cpp"])
   if not owned_flags or owned_flags[-1] != expected_debug:
      raise RuntimeError(f"forge_owned.cpp: Debug optimization changed unexpectedly: {owned_flags!r}")
   if expected_sanitizer not in by_name["forge_owned.cpp"]:
      raise RuntimeError("forge_owned.cpp: sanitizer fixture option is missing")
   if any(flag.startswith("-fno-sanitize=") for flag in sanitizer_flags(by_name["forge_owned.cpp"])):
      raise RuntimeError("forge_owned.cpp: sanitizer coverage was weakened")

   print(
      "vendored C/C++ sources preserve parent sanitizers; selected vendor sources disable only alignment; "
      "Forge-owned sanitizer coverage is preserved"
   )
   return 0


if __name__ == "__main__":
   raise SystemExit(main())

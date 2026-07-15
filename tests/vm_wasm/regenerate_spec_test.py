#!/usr/bin/env python3
"""Run the pinned donor generator and map its output to the Forge Boost.Test port."""

from __future__ import annotations

import argparse
import pathlib
import subprocess

from port_donor_tests import transform_text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=pathlib.Path, required=True)
    parser.add_argument("--input", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    generated = subprocess.check_output(
        [str(arguments.generator), str(arguments.input)],
        text=True,
    )
    relative = pathlib.Path("spec") / arguments.output.name
    arguments.output.write_text(transform_text(generated, relative))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

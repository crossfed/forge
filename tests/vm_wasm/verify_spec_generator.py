#!/usr/bin/env python3
"""Verify that the donor generator and Forge mapping reproduce the golden output."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=pathlib.Path, required=True)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temporary:
        output = pathlib.Path(temporary) / "generator_expected.cpp"
        subprocess.run(
            [
                "python3",
                str(args.source / "regenerate_spec_test.py"),
                "--generator",
                str(args.generator),
                "--input",
                str(args.source / "generator_fixture.json"),
                "--output",
                str(output),
            ],
            check=True,
        )
        expected = args.source / "generator_expected.cpp"
        if output.read_bytes() != expected.read_bytes():
            raise RuntimeError("adapted specification generator output differs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

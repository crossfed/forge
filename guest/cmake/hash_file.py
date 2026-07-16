#!/usr/bin/env python3

import argparse
import hashlib
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    digest = hashlib.sha256(args.input.read_bytes()).hexdigest()
    args.output.write_text(f"{digest}  {args.input.name}\n", encoding="utf-8")


if __name__ == "__main__":
    main()

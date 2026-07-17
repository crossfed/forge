#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import pathlib
from typing import Dict, List, Optional, Tuple


def read_golden(path: pathlib.Path) -> Dict[str, Tuple[List[str], Optional[str]]]:
    result = {}
    for source_line in path.read_text(encoding="utf-8").splitlines():
        line = source_line.strip()
        if not line or line.startswith("#"):
            continue
        name, parameters, return_type = line.split("|")
        if name in result:
            raise RuntimeError(f"duplicate golden intrinsic: {name}")
        result[name] = (parameters.split(",") if parameters else [], return_type or None)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--golden", required=True, type=pathlib.Path)
    parser.add_argument("--eosio-db-header", required=True, type=pathlib.Path)
    parser.add_argument("--cdt-fixture", required=True, type=pathlib.Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    imports = manifest["imports"]
    names = [entry["import"] for entry in imports]
    database = {entry["import"]: entry for entry in imports if entry["import"].startswith("db_")}
    golden = read_golden(args.golden)

    assert manifest["schema_version"] == 1
    assert manifest["interface_version"] == 1
    assert len(imports) == 68
    assert len(names) == len(set(names))
    assert len(database) == 60
    assert database.keys() == golden.keys()

    for name, entry in database.items():
        expected_parameters, expected_result = golden[name]
        assert entry["interface_version"] == 1
        assert entry["identifier"] == name
        assert entry["module"] == "env"
        assert entry["wasm_parameters"] == expected_parameters
        assert entry["wasm_result"] == expected_result

    eosio_header = args.eosio_db_header.read_text(encoding="utf-8")
    assert eosio_header == "#pragma once\n\n#include <forge/contract/intrinsics.h>\n"

    fixture = args.cdt_fixture.read_bytes()
    assert hashlib.sha256(fixture).hexdigest() == "82683e6ab0732382df65266f3cc24428dab6dd658d37dc28457976431624f260"
    donor_root = os.environ.get("FORGE_CONTRACT_CDT_DONOR")
    if donor_root:
        donor = pathlib.Path(donor_root) / "tests/unit/test_contracts/capi/db.c"
        assert fixture == donor.read_bytes()


if __name__ == "__main__":
    main()

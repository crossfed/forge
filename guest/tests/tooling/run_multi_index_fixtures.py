#!/usr/bin/env python3

import argparse
import json
import pathlib


CDT_COMMIT = "69599db279b7b93d0688502720c15c6962a1401b"
SPRING_COMMIT = "e6a99f68b67abc4d89fe716755b2e1394a4991f7"
DONOR_CASES = {
    "idx64_store_only",
    "idx64_check_without_storing",
    "idx64_general",
    "idx128_store_only",
    "idx128_check_without_storing",
    "idx128_general",
    "idx64_require_find_fail",
    "idx64_require_find_fail_with_msg",
    "idx64_require_find_sk_fail",
    "idx64_require_find_sk_fail_with_msg",
    "idx128_autoincrement_test",
    "idx128_autoincrement_test_part1",
    "idx128_autoincrement_test_part2",
    "idx256_general",
    "idx_double_general",
    "idx_long_double_general",
    "idx64_pk_iterator_exceed_end",
    "idx64_sk_iterator_exceed_end",
    "idx64_pk_iterator_exceed_begin",
    "idx64_sk_iterator_exceed_begin",
    "idx64_pass_pk_ref_to_other_table",
    "idx64_pass_sk_ref_to_other_table",
    "idx64_pass_pk_end_itr_to_iterator_to",
    "idx64_pass_pk_end_itr_to_modify",
    "idx64_pass_pk_end_itr_to_erase",
    "idx64_pass_sk_end_itr_to_iterator_to",
    "idx64_pass_sk_end_itr_to_modify",
    "idx64_pass_sk_end_itr_to_erase",
    "idx64_modify_primary_key",
    "idx64_run_out_of_avl_pk",
    "idx64_sk_cache_pk_lookup",
    "idx64_pk_cache_sk_lookup",
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mapping", required=True, type=pathlib.Path)
    parser.add_argument("--abi-pair", action="append", nargs=2, default=[], type=pathlib.Path)
    args = parser.parse_args()

    mapping = json.loads(args.mapping.read_text(encoding="utf-8"))
    assert mapping["schema_version"] == 1
    assert mapping["donors"] == {"cdt": CDT_COMMIT, "spring": SPRING_COMMIT}

    cases = mapping["cases"]
    names = [case["name"] for case in cases]
    assert len(names) == len(set(names))
    assert set(names) == DONOR_CASES
    for case in cases:
        assert case["scenarios"]
        assert all(isinstance(value, int) and 0 <= value <= 31 for value in case["scenarios"])

    assert args.abi_pair
    for modern_path, legacy_path in args.abi_pair:
        modern = json.loads(modern_path.read_text(encoding="utf-8"))
        legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
        assert modern == legacy, f"modern and EOSIO ABI differ: {modern_path.name}, {legacy_path.name}"
        tables = {table["name"]: table for table in modern["tables"]}
        assert tables["records"]["type"] == "record"
        structs = {shape["name"]: shape for shape in modern["structs"]}
        fields = {field["name"]: field["type"] for field in structs["record"]["fields"]}
        assert fields["secondary256"] == "checksum256"
        assert fields["secondary_long_double"] == "float128"


if __name__ == "__main__":
    main()

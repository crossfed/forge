# Forge Chain Quorum And Fork Donor Baseline

## Versions

- Spring: `vbytemaster/spring@e6a99f68b67abc4d89fe716755b2e1394a4991f7`
- Blockchain implementation reference:
  `vbytemaster/blockchain@bc3159a18d06197ebbe61a1a0d293aae8c91cc39`

The donor source is a behavioral oracle and is not a build dependency.

## Inspected Sources

- Spring `libraries/chain/fork_database.cpp` and
  `libraries/chain/include/eosio/chain/fork_database.hpp`
- Spring `unittests/fork_db_tests.cpp`
- Blockchain `libraries/chain/consensus`
- Blockchain `libraries/chain/fork`
- Blockchain `tests/unit/libraries/chain/runtime_tests.cpp`

## Accepted

- Checked `std::uint64_t` weighted-quorum arithmetic.
- Duplicate and out-of-range signer rejection.
- Separate indexes for node ID, ancestry and deterministic best-rank selection.
- Thread-safe reads and mutations.
- Root advancement, common-ancestor lookup, path construction and subtree
  pruning.
- Savanna-style deterministic ID tie-breaking after rank comparison.

## Rejected

- Spring block-state types, persistence and legacy/Savanna switching.
- Controller transitions, pending irreversible state and block-log ownership.
- Product-specific exception names and namespaces.
- Linear scanning of all nodes for every best-branch query.

## Forge Evidence

- Quorum tests cover thresholds, malformed signer sets and overflow.
- Fork tests cover the full graph lifecycle, concurrent access and
  comparator-instrumented constant-time best lookup.
- Package consumers prove independent `chain_quorum` and `chain_fork`
  components.

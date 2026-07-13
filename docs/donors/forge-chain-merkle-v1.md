# Forge Chain Modern Merkle Donor Baseline v1

This note records the Spring behavior used to add modern Merkle primitives to
`forge_chain_core` and its integration with `forge_chain_protocol`. Forge owns
an independent C++23 implementation and uses Spring as a protocol compatibility
donor.

## Donor Snapshot

- Repository: local `AntelopeIO/spring` donor checkout.
- Commit: `e6a99f68b`.
- Inspected files:
  - `libraries/chain/include/eosio/chain/merkle.hpp`;
  - `libraries/chain/include/eosio/chain/incremental_merkle.hpp`;
  - `libraries/chain/controller.cpp` transaction-root integration;
  - `unittests/merkle_tree_tests.cpp` batch/incremental parity coverage.

## Accepted Behavior

- The empty tree root is the zero digest.
- A single leaf is already the root.
- Power-of-two ranges form balanced binary trees.
- Other ranges split at the largest power-of-two prefix and recursively hash
  the remaining suffix.
- Pair hashing writes the left digest followed by the right digest into a
  SHA-256 encoder.
- Incremental state stores the appended leaf count as `mask`, followed by roots
  of the active power-of-two subtrees as `trees`.
- The incremental root equals the batch root after every append.
- Transaction Merkle leaves are `transaction_receipt::digest()` values.

## Rejected Behavior

- The legacy EOSIO algorithm, including odd-leaf duplication and left/right bit
  marking, is not supported.
- Spring's internal Merkle worker pool is runtime scheduling policy and is not
  part of `forge_chain_core`.
- Controller, block assembly, fork database and finality policy remain outside
  the Chain family.
- No `calculate_action_mroot()` helper is provided. Modern Spring/Savanna uses
  the block header's `action_mroot` as a finality-tree root. The separate
  Merkle root of Savanna action receipt digests is documented in
  `forge-chain-savanna-action-receipt-v1.md`.

## Verified Invariants

- Fixed roots for zero through nine `NodeN` digest leaves match the modern
  Spring algorithm.
- Batch and incremental roots match after each append through 1001 leaves.
- Raw incremental state uses Spring field order `mask`, then `trees`, and can be
  restored before further appends.
- Invalid raw state where `trees.size() != popcount(mask)` is rejected.
- `calculate_transaction_mroot()` hashes receipt digests in block order.

## Follow-Up Chain Gaps

Separate donor-backed work is still required for TaPoS reference-block helpers,
typed extension validation, Spring-compatible producer signing context and
Savanna finality primitives. Those concerns are intentionally not folded into
the Merkle component.

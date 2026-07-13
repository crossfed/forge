# Forge Chain Savanna Action Receipt Donor Baseline v1

This note records the Spring protocol behavior used for
`forge.chain.protocol.action` and `forge.chain.protocol.action_receipt`. Forge
owns an independent C++23 implementation and uses Spring only as a wire and
hash oracle.

## Donor Snapshot

- Repository: local `AntelopeIO/spring` donor checkout.
- Commit: `e6a99f68b67abc4d89fe716755b2e1394a4991f7`.
- Inspected files:
  - `libraries/chain/include/eosio/chain/action.hpp`;
  - `libraries/chain/include/eosio/chain/action_receipt.hpp`;
  - `libraries/chain/include/eosio/chain/trace.hpp`;
  - `libraries/chain/apply_context.cpp`;
  - `libraries/chain/include/eosio/chain/transaction_context.hpp`;
  - `libraries/chain/controller.cpp`;
  - `libraries/chain/include/eosio/chain/block_header.hpp`.

## Accepted Protocol Behavior

- `action_receipt` serializes these fields in order: `receiver`, `act_digest`,
  `global_sequence`, `recv_sequence`, `auth_sequence`, `code_sequence`, then
  `abi_sequence`.
- `auth_sequence` uses the sorted `fc::flat_map` wire shape: varuint element
  count followed by ordered key/value pairs.
- The executed action digest first hashes packed `action_base`, separately
  hashes the packed pair of action data and return value, then hashes the pair
  of those digests.
- The Savanna witness hash commits to `global_sequence`, `auth_sequence`,
  `code_sequence` and `abi_sequence` in that order.
- The Savanna action receipt digest commits to `receiver`, `recv_sequence`,
  executed action account and name, `act_digest`, and the witness hash.
- Executed Savanna action receipt digests can be reduced with the modern Merkle
  algorithm already owned by `forge_chain_core`.

## Root Semantics

Spring maintains a Merkle root of executed Savanna action receipt digests for
finality data and action proofs. That root is not the same concept as the value
stored in `block_header.action_mroot` for a proper Savanna block. Spring
repurposes the historical header field as the finality-tree root claim, while
the action receipt root is carried through finality state/data. Forge therefore
provides receipt digest leaves and the generic modern Merkle primitive, but does
not expose a misleading `calculate_action_mroot(block_header)` helper.

## Rejected Donor Behavior

- Legacy action receipt digests and legacy action Merkle roots are not
  supported.
- Transition storage modes such as legacy, both, or Savanna are not part of the
  Forge API; the target Blockchain starts with Savanna at genesis.
- `action_trace` is execution runtime state, not a protocol DTO. Console output,
  elapsed time, exceptions, RAM deltas, inline traces and block context remain
  in the Blockchain repository.
- Controller scheduling, finality-tree maintenance and block assembly remain
  outside `forge_chain_protocol`.

## Oracle Coverage

- Hard-coded Spring bytes cover the complete seven-field receipt, sorted
  authorization sequences and varuint boundaries for code/ABI sequences.
- Hard-coded hashes cover empty and non-empty return values, witness hashing,
  the final Savanna receipt digest and a two-leaf modern Merkle root.
- Raw and variant round trips cover the generic `std::flat_map` support used by
  the receipt.

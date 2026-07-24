# Forge Chain Savanna

`forge_chain_savanna` is the Preview operational kernel for Savanna-style
instant finality. It owns policy validation, strong/weak finality transitions,
quorum-certificate verification, validation commitments and deterministic fork
ranking without owning a blockchain controller.

## Package

```cmake
find_package(Forge CONFIG REQUIRED COMPONENTS chain_savanna)
target_link_libraries(my_target PRIVATE Forge::forge_chain_savanna)
```

The component automatically provides Chain Core, Chain Quorum, Crypto BLS,
Crypto Digest, Raw and Variant dependencies.

## Modules

- `forge.chain.savanna.types`
- `forge.chain.savanna.policy`
- `forge.chain.savanna.finality_core`
- `forge.chain.savanna.qc`
- `forge.chain.savanna.validation`
- `forge.chain.savanna.rank`
- `forge.chain.savanna.exceptions`

## Operational Types

Block number and slot are explicit `std::uint32_t` values. `block_ref` does not
derive a number from a protocol-specific block ID:

```cpp
import forge.chain.savanna.finality_core;

namespace savanna = forge::chain::savanna;

auto core = savanna::finality_core::genesis(0, 10);

core = core.next(
    savanna::block_ref{
        .num = 0,
        .id = genesis_id,
        .slot = 10,
        .finality_digest = genesis_finality_digest,
        .active_policy_generation = 1,
    },
    savanna::qc_claim{.block = 0, .strong = true});
```

Finalizer policies require a non-empty unique BLS key set without the identity
key, checked total weight and a reachable strict-majority threshold. Admission
also supplies one BLS proof of possession per finalizer. `validate()` returns a
non-serializable `verified_finalizer_policy`, and QC verification accepts only
that verified capability. The raw policy remains the donor-compatible
operational record and is available through `get()`.

Policy generations advance exactly one step. Ordered diffs use ascending
removal and insertion indexes. `apply()` reuses verification for retained keys
and requires proofs aligned with newly inserted finalizers. A downstream
adapter must retain or recover registration proofs when reconstructing an
operational verified policy after restart.

## Compatibility Invariants

`finality_core::pack_for_digest()` preserves the Spring consensus preimage. A
block reference contributes only `id`, `slot` and `finality_digest`; explicit
block number and policy generations are deliberately excluded. Raw layouts and
digest fixtures are covered by donor-backed tests.

QC verification delegates weighted threshold evaluation to
`forge_chain_quorum` and grouped aggregate signature verification to
`forge_crypto_bls`. Same-message key aggregation is available only after
proof-of-possession validation, preventing rogue-key attacks. A finalizer
present in both active and pending policies must cast the same strong/weak vote
in both signatures. A weak QC must contain at least one weak vote, and its
strong-vote subset must remain below threshold, so a strong quorum cannot be
represented non-canonically as weak.

`validation_state` retains neutral leaf preimages alongside historical roots.
Validation checks their explicit contiguous block numbers from `first`, hashes
and replays every leaf, verifies every retained root and compares the complete
incremental Merkle state. This makes deserialized state safe to query through
`root_at()` after validation without a full protocol block history.
Chain code does not include or call the BLS vendor.

## Boundaries

This library does not own protocol blocks, producer schedules, genesis
configuration, header extensions, signed-block admission, controller state,
execution, persistence, P2P synchronization, node lifecycle, key custody or
product policy. Those layers adapt their own protocol records into the
operational types here.

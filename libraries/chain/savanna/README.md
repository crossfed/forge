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
key, checked total weight and a reachable strict-majority threshold. Policy
generations advance exactly one step. Ordered diffs use ascending removal and
insertion indexes.

## Compatibility Invariants

`finality_core::pack_for_digest()` preserves the Spring consensus preimage. A
block reference contributes only `id`, `slot` and `finality_digest`; explicit
block number and policy generations are deliberately excluded. Raw layouts and
digest fixtures are covered by donor-backed tests.

QC verification delegates weighted threshold evaluation to
`forge_chain_quorum` and grouped aggregate signature verification to
`forge_crypto_bls`. A finalizer present in both active and pending policies must
cast the same strong/weak vote in both signatures. Chain code does not include
or call the BLS vendor.

## Boundaries

This library does not own protocol blocks, producer schedules, genesis
configuration, header extensions, signed-block admission, controller state,
execution, persistence, P2P synchronization, node lifecycle, key custody or
product policy. Those layers adapt their own protocol records into the
operational types here.

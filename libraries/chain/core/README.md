# forge_chain_core

`forge_chain_core` provides fundamental deterministic mechanisms shared by
blockchain protocols. Use it when code needs the canonical Forge chain digest
or modern Merkle calculation without depending on transaction or block wire
types.

Package component: `chain_core`. Public namespace: `forge::chain::core`.

## Modules

- `forge.chain.core.types` defines `digest` as `forge::crypto::sha256`.
- `forge.chain.core.merkle` exports `calculate_merkle_root()` and
  `incremental_merkle_tree` and re-exports `forge.chain.core.types`.

The target publicly links `forge_crypto`, `forge_exceptions` and `forge_raw`.

## Merkle Roots

```cpp
#include <array>
#include <span>

import forge.chain.core.merkle;

const auto leaves = std::array{
   forge::chain::core::digest::hash("transaction-1"),
   forge::chain::core::digest::hash("transaction-2"),
};

const auto root = forge::chain::core::calculate_merkle_root(
   std::span<const forge::chain::core::digest>{leaves});
```

An empty leaf range produces the zero digest. A single leaf is returned as-is.
Incomplete trees split at the largest lower power of two; the final leaf is not
duplicated.

Use `incremental_merkle_tree` when leaves arrive over time:

```cpp
auto tree = forge::chain::core::incremental_merkle_tree{};
tree.append(forge::chain::core::digest::hash("transaction-1"));
tree.append(forge::chain::core::digest::hash("transaction-2"));
const auto root = tree.root();
```

Its raw state is encoded as `mask` followed by `trees`. Malformed state whose
tree count does not match the mask is rejected with a typed raw codec error.

## Boundaries

Core does not contain protocol IDs, transactions, blocks, authorities, ABI,
Merkle proofs, controller behavior, execution or finality policy. It does not
perform parallel work; callers decide scheduling and batching.

Do not treat arbitrary text or JSON as a consensus preimage. Callers must hash
the canonical bytes defined by their protocol.

## Tests

`test_forge_chain_core` covers hardcoded roots, left/right ordering,
batch/incremental parity, raw state compatibility, malformed state and append
overflow. `test_forge_package_chain_core_component` verifies the installed
`chain_core` component and module imports.

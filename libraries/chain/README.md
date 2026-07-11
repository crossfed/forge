# Forge Chain Family

The Chain family separates fundamental chain mechanisms from canonical protocol
records. The root `forge::chain` namespace is a grouping namespace and owns no
public symbols.

## Libraries

### `forge_chain_core`

Package component: `chain_core`.

- `forge.chain.core.types` owns the canonical chain `digest`.
- `forge.chain.core.merkle` owns modern Merkle root calculation and incremental
  Merkle state.

Dependencies: `forge_crypto`, `forge_exceptions` and `forge_raw`.

```cpp
#include <span>

import forge.chain.core.merkle;

const auto leaf = forge::chain::core::digest::hash("block");
const auto root = forge::chain::core::calculate_merkle_root(std::span{&leaf, 1U});
```

### `forge_chain_protocol`

Package component: `chain_protocol`.

- `forge.chain.protocol.types` owns names, assets, symbols, IDs, keys,
  signatures, timestamps and scalar wire vocabulary.
- `forge.chain.protocol.fixed_key` owns `fixed_key<Size>` and `key256`, with
  canonical raw bytes, ordered word construction and fixed-width hex variants.
- `forge.chain.protocol.authority` owns permission weights and authority
  thresholds.
- `forge.chain.protocol.transaction` owns actions, transactions, packed
  transactions, IDs and signing digests.
- `forge.chain.protocol.block` owns block headers, receipts, signed blocks,
  block IDs and transaction receipt Merkle integration.
- `forge.chain.protocol.abi` owns ABI records and optional tail-field decoding.
- `forge.chain.protocol.system` owns canonical system action payloads.

Dependencies: `forge_chain_core`, `forge_compression`, `forge_raw`,
`forge_variant` and `forge_crypto`.

```cpp
#include <deque>

import forge.chain.protocol.block;
import forge.chain.protocol.fixed_key;

auto receipts = std::deque<forge::chain::protocol::transaction_receipt>{};
auto header = forge::chain::protocol::block_header{};
header.transaction_mroot = forge::chain::protocol::calculate_transaction_mroot(receipts);

auto key = forge::chain::protocol::key256::make_from_word_sequence<std::uint64_t>(
   0, 0, 0, 42);
```

## Boundaries

- Core does not import protocol.
- Protocol owns wire field order, raw compatibility and deterministic signing
  rules, and may build on core algorithms.
- Products own controller, state, execution, consensus, finality, P2P sync,
  node lifecycle, runtime config and key custody.
- Text and JSON are not signing representations; canonical preimages use
  `forge::raw::pack` and protocol helpers.
- The legacy Merkle algorithm, Merkle proofs and action/finality-root
  calculation are not part of the current family.

## Tests

`test_forge_chain_core` covers Merkle golden roots, incremental parity, raw
state compatibility and malformed/overflow behavior.

`test_forge_chain_protocol` covers raw and variant fixtures, fixed keys,
names/assets, transactions, ABI, block IDs, receipt digests, compression and
signatures.

Installed package surfaces are covered independently by
`test_forge_package_chain_core_component` and
`test_forge_package_chain_protocol_component`.

# Forge Chain Family

The Chain family separates reusable blockchain mechanisms from canonical
protocol records. The root `forge::chain` namespace is a grouping namespace and
owns no public symbols.

## Libraries

- [`forge_chain_core`](core/README.md) owns the canonical digest and modern
  Merkle primitives. Package component: `chain_core`.
- [`forge_chain_protocol`](protocol/README.md) owns protocol values,
  fixed-size ordered keys, transactions, blocks, ABI, authorities and system
  payloads. Package component: `chain_protocol`.
- [`forge_chain_quorum`](quorum/README.md) owns deterministic weighted-quorum
  evaluation. Package component: `chain_quorum`.
- [`forge_chain_fork`](fork/README.md) owns a generic thread-safe fork graph and
  deterministic best-rank selection. Package component: `chain_fork`.
- [`forge_chain_savanna`](savanna/README.md) owns a neutral operational
  finality kernel, finalizer policy validation and typed QC verification.
  Package component: `chain_savanna`.

The dependency direction is `forge_chain_protocol -> forge_chain_core`. Core
never imports protocol.

Focused leaves may own neutral consensus mechanisms such as quorum evaluation,
fork tracking and finality algorithms. Controller state, execution, state
persistence, P2P synchronization, node lifecycle, runtime configuration, key
custody and product policy remain outside the Chain family.

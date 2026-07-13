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

The dependency direction is `forge_chain_protocol -> forge_chain_core`. Core
never imports protocol.

Controller state, execution, consensus, finality, P2P synchronization, node
lifecycle, runtime configuration and key custody belong to products or future
focused libraries, not to this family root.

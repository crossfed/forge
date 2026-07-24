# Forge Chain Quorum

`forge_chain_quorum` provides deterministic weighted-quorum evaluation for
consensus and authorization algorithms. The public C++ API is **Preview**.

Package component: `chain_quorum`.

## Modules

- `forge.chain.quorum.types`
- `forge.chain.quorum.evaluate`
- `forge.chain.quorum.exceptions`

```cpp
import forge.chain.quorum.evaluate;

const auto weights = std::array<std::uint64_t, 3>{2, 3, 5};
const auto signers = std::array<std::uint32_t, 2>{0, 2};
const auto result = forge::chain::quorum::evaluate(7, weights, signers);
```

The evaluator rejects duplicate and out-of-range signer indices and detects
`std::uint64_t` total-weight overflow. A zero threshold is reached even when
the signer set is empty.

This library does not own votes, signatures, membership policy, networking,
finality or controller state. Callers supply an ordered weight set and signer
indices produced by their own validated policy.

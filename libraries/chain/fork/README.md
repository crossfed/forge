# Forge Chain Fork

`forge_chain_fork` provides an in-memory, thread-safe fork graph with
deterministic best-branch selection. The public C++ API is **Preview**.

Package component: `chain_fork`.

## Modules

- `forge.chain.fork.types`
- `forge.chain.fork.graph`
- `forge.chain.fork.exceptions`

```cpp
import forge.chain.fork.graph;

forge::chain::fork::graph<block_id, rank, candidate> graph;
graph.reset(root);
graph.insert(child);
const auto best = graph.best();
```

Insert, lookup and best-rank maintenance are logarithmic. `best()` reads the
first entry of the maintained rank index and does not scan all nodes. When two
entries have equivalent ranks, the greater ID wins deterministically.

The graph owns no persistence, controller transitions, execution, consensus
policy or networking. `Value` is copied out of the graph, so callers cannot
mutate parent or rank keys behind its indexes.

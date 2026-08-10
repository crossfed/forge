# forge.chain.transaction

Local construction and signing of canonical Forge chain transactions.

The library owns TAPOS, expiration, resource limits, action assembly, local
signing orchestration and packing. It does not perform API calls, resolve ABIs,
load profiles or access a network transport. Wire records and digest semantics
remain owned by `forge.chain.protocol`.

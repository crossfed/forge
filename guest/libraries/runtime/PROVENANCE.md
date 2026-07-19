# Runtime Provenance

`allocator.cpp` is derived from the active allocator implementation in
AntelopeIO CDT commit `69599db279b7b93d0688502720c15c6962a1401b`, file
`libraries/eosiolib/malloc.cpp`. The allocator algorithms, block metadata,
reuse, coalescing, in-place reallocation and WebAssembly memory growth behavior
are retained. Includes, namespace, check policy, formatting and overflow
handling were adapted to Forge Contract SDK.

The donor is licensed under the EOSIO/Antelope license carried by the donor
repository. Forge's distribution notices must include that license.

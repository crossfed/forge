# Forge Chain Savanna v1 Donor Note

## Scope

This note records the donor baseline for the Preview
`forge_chain_savanna` operational finality kernel and grouped BLS
verification.

Consensus-critical Raw and digest fixtures are compatibility invariants even
while the public C++ API remains Preview.

## Donors

### Spring

- Repository: local `spring` donor checkout.
- Commit: `e6a99f68`.
- Reviewed:
  - `libraries/chain/include/eosio/chain/finality_core.hpp`
  - `libraries/chain/finality_core.cpp`
  - `libraries/chain/include/eosio/chain/finalizer_policy.hpp`
  - `libraries/chain/include/eosio/chain/qc.hpp`
  - `libraries/chain/qc.cpp`
  - `libraries/chain/include/eosio/chain/block_state.hpp`
  - `libraries/chain/block_state.cpp`
  - `libraries/chain/include/eosio/chain/finalizer.hpp`
  - `libraries/chain/finalizer.cpp`
  - `libraries/testing/contracts/eosio.bios/eosio.bios.hpp`
  - `libraries/testing/contracts/eosio.bios/eosio.bios.cpp`
  - `unittests/finality_core_tests.cpp`
  - `unittests/savanna_finalizer_policy_tests.cpp`
  - `unittests/finalizer_vote_tests.cpp`
  - `unittests/finalizer_tests.cpp`
  - `unittests/block_state_tests.cpp`

Accepted:

- strong and weak QC transition rules;
- ordered finality links and reversible block references;
- reversible-reference Merkle construction;
- ordered finalizer policy remove/insert diffs;
- active and pending policy QC signatures;
- identical vote semantics for finalizers shared by active and pending policies;
- strong digest plus `WEAK` postfix for weak votes;
- finality digest packing that omits finalizer-policy generations.
- proof-of-possession validation when finalizer keys enter policy state.
- front-trimming validation roots at the last-final boundary while retaining
  the complete incremental Merkle frontier;
- thread-safe strong/weak vote accumulation with the donor states
  `unrestricted`, `restricted`, `weak_achieved`, `weak_final` and `strong`;
- best-QC selection between locally aggregated and externally received
  certificates;
- cross-candidate finalizer safety through `last_vote`, `lock` and
  `other_branch_latest_time`;
- durable-before-signing ownership for finalizer safety state.

The Spring finality preimage is preserved exactly in structure:

1. Raw `links`;
2. variable-length reference count;
3. for each reference: block ID, slot, finality digest;
4. genesis slot.

Forge's explicit block number is not packed into this preimage.

Rejected:

- deriving block number from a protocol-specific block ID;
- controller, fork database and header-state ownership;
- producer schedules, genesis policy and extension IDs;
- vote networking, connection/runtime queues and key custody;
- Spring's controller-owned safety file, CRC and filesystem lifecycle;
- serialization of ephemeral vote accumulators;
- direct vendor cryptography in Chain.

### Blockchain Reference

- Repository: local `blockchain` implementation reference.
- Commit: `bc3159a18d06197ebbe61a1a0d293aae8c91cc39`.
- Reviewed:
  - `libraries/chain/savanna/{types,policy,finality_core,qc,validation,rank}.cpp`
  - matching module interfaces;
  - `tests/unit/libraries/chain/runtime_tests.cpp`.

Accepted:

- Forge Raw/Describe integration;
- typed exceptions;
- separation of policy, finality, QC, validation and rank components;
- product-neutral commitment Merkle state.

Corrected during transfer:

- `block_ref` owns explicit `num` and `slot`;
- identity BLS keys are rejected before they can contribute quorum weight;
- Raw-described commitment records are hashed once, without repacking an
  already serialized byte vector;
- validation uses neutral `commitment`, not an action-receipt field;
- rank consumes `finality_core + block_ref`, not header state;
- grouped BLS verification moves into `forge_crypto_bls`;
- grouped verification accepts only proof-verified public-key capabilities,
  while Raw finalizer policies keep the donor layout without embedded proofs;
- weak QC encoding rejects a present-but-empty weak-vote bitset;
- weak QC encoding is rejected when its strong votes already reach quorum;
- retained validation roots and their starting block are replay-verified from
  stored neutral leaf preimages and a compact prefix frontier rather than
  trusted as an unauthenticated historical cache;
- validation roots and preimages are bounded by explicit finality advancement;
- append no longer replays retained history;
- QC weight evaluation uses `forge_chain_quorum`;
- QC verification returns a non-serializable capability bound to the checked
  finality digest and strong voters;
- active and pending policy halves remain paired during received/local
  best-certificate selection;
- shared active/pending finalizers are mutated atomically after signature
  verification outside the accumulator mutex;
- finalizer safety is a versioned neutral value while durable persistence
  remains downstream;
- producer and protocol records remain downstream.

## Target Components

- `forge_crypto_bls`
  - grouped aggregate verification over typed public-key/message groups;
  - only this Crypto leaf calls `bls12-381`.
- `forge_chain_savanna`
  - operational types;
  - policy validation and diffs;
  - finality transitions and digest;
  - QC validation;
  - local and received QC aggregation;
  - bounded validation commitment tree;
  - finalizer vote safety planning;
  - deterministic rank.

## Verification

The focused tests cover:

- Spring-compatible `qc_claim` Raw bytes and full QC layout checksum;
- hardcoded finality digest preimages;
- strong/weak transition sequences;
- reversible Merkle roots;
- policy and ordered-diff rejection;
- duplicate BLS keys and weight overflow;
- grouped active/pending QC verification and tampering;
- malformed vote bitsets;
- validation commitment roots, compaction and 100,000-block bounded-state
  behavior;
- all donor accumulator states, duplicate/conflicting votes, active/pending
  policies, concurrent submissions and best-QC selection;
- monotonicity, liveness, ancestry safety, branch changes, restart roundtrip
  and verified strong-QC safety advancement;
- rank ordering;
- static absence of protocol, product and direct BLS-vendor dependencies in
  the Chain leaf.

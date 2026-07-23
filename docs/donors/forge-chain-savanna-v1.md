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
  - `libraries/testing/contracts/eosio.bios/eosio.bios.hpp`
  - `libraries/testing/contracts/eosio.bios/eosio.bios.cpp`
  - `unittests/finality_core_tests.cpp`
  - `unittests/savanna_finalizer_policy_tests.cpp`

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
- vote networking, aggregation runtime and key custody;
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
- QC weight evaluation uses `forge_chain_quorum`;
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
  - validation commitment tree;
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
- validation commitment roots;
- rank ordering;
- static absence of protocol, product and direct BLS-vendor dependencies in
  the Chain leaf.

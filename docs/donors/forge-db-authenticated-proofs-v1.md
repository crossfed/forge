# Forge DB authenticated proof donor manifest v1

This manifest records proof-security evidence only. Forge owns its hash schema,
DTOs, binary codec and ranked range verifier; donor formats are not product
dependencies.

## Sources

| Source | Pin | License | Inspected evidence |
|---|---|---|---|
| `cosmos/ics23` | Go verifier commit `7f2c2d0965fdcf33658cce3198ddae078a449fc2`; IAVL fixtures from tag `v0.7.1`, commit `014bd93b66bb57e5f250be0c9a344505f7d0fa70` | Apache-2.0 | Official IAVL existence and non-existence vectors plus `VerifyMembership` and `VerifyNonMembership` behavior |
| `cosmos/iavl` Dragonfruit fix | Release `v0.19.3`, commit `7f698ba3fa232c54109e5b4ea42562bbecdb1bf8` | Apache-2.0 | `proof.go` ambiguity fix and `proof_forgery_test.go` regression: a proof inner node must not accept simultaneous left and right child hashes |
| Cosmos security advisory | `Cosmos-SDK Security Advisory Dragonfruit`, 2022-10-08 | Documentation reference | High-severity warning for legacy IAVL `RangeProof` and recommendation to avoid that native proof format |

Upstream references:

- <https://github.com/cosmos/ics23>
- <https://github.com/cosmos/iavl/commit/7f698ba3fa232c54109e5b4ea42562bbecdb1bf8>
- <https://forum.cosmos.network/t/cosmos-sdk-security-advisory-dragonfruit/7614>

## Decisions

Accepted patterns:

- independent membership and non-membership verification against fixed roots;
- exactly one typed sibling per point-proof step, making the Dragonfruit
  simultaneous-left-and-right state structurally unrepresentable;
- exact root and tree-size binding before accepting point or range results;
- complete preorder consumption, strict key ordering, authenticated subtree
  sizes and ranks, and explicit lower/upper boundary witnesses;
- malformed, omitted, duplicated and reordered witness nodes as mandatory
  negative coverage.

Rejected patterns:

- legacy IAVL `RangeProof` DTOs or verification behavior;
- relabelling Forge proof JSON, raw bytes or hash schema as ICS23;
- using the official Go verifier as a Forge product runtime dependency;
- accepting a reconstructed root without separately proving requested range
  continuity and certified boundaries.

## Forge evidence

- [Official ICS23 and independent Forge Go lanes](../../tests/db_authenticated/ics23_harness/README.md)
- [Forge point membership vector](../../tests/db_authenticated/ics23_harness/vectors/forge_point_membership_v3.json)
- [Forge point non-membership vector](../../tests/db_authenticated/ics23_harness/vectors/forge_point_nonmembership_v3.json)
- [Native golden and ranked-range adversarial tests](../../tests/db_authenticated/ranked_range_proof_adversarial_tests.cpp)

The ranked-range corpus rejects malformed metadata, omitted nodes, duplicated
nodes, reordered leaves, an omitted lower predecessor and an omitted upper
boundary while preserving a positive root/rank baseline.

## Limits

- The independent Go verifier covers Forge v3 state-tree point proofs only. It
  does not decode Forge raw proof bytes or verify ranked range/change proofs.
- The official ICS23 lane verifies official IAVL protobuf vectors only. Forge
  still has no native ICS23 codec or verifier and claims no ICS23 wire
  compatibility.
- These tests reduce implementation variance; they do not replace the external
  cryptographic review required before production use.

# Crypto Signer Binary API Donor Baseline v1

This note records the donor evidence for returning typed binary key and
signature values from the local Crypto Signer API. Text encodings remain
configuration and external-adapter concerns.

## Source Reviewed

BitShares/Graphene commit
`b92b82ba3e57381d111f28383e8cfa89a8356966`:

- `libraries/protocol/include/graphene/protocol/types.hpp`, where
  `signature_type` aliases `fc::ecc::compact_signature`;
- `libraries/protocol/include/graphene/protocol/transaction.hpp`, where signing
  returns `signature_type` and `signed_transaction` stores
  `std::vector<signature_type>`.

## Accepted

- Protocol and service models carry binary signature value types rather than
  profile-formatted text.
- Signature serialization is owned by the binary codec of the value type.
- Text formatting is explicit at JSON, CLI, configuration or other textual
  boundaries.

## Adapted For Forge

- Forge returns `forge::crypto::asymmetric::signature` instead of Graphene's
  secp256k1-specific compact signature. The Forge value also represents P-256,
  Ed25519 and RSA signatures.
- Forge returns the matching `forge::crypto::asymmetric::public_key` so callers
  do not need to parse a profile-formatted key.
- Optional algorithm and purpose checks remain signer-plugin policy; they are
  not protocol serialization fields.

## Rejected

- Graphene wallet, key-store and transaction-builder behavior is not copied
  into the Forge plugin.
- No implicit Antelope, Graphene or Forge text profile is selected by `sign()`.
- No compatibility response fields or automatic binary-to-text conversion are
  retained in the version 2 contract.

## Verification

- secp256k1, P-256, Ed25519 and RSA typed responses are verified with their
  matching public keys;
- public keys, signatures and the complete response round-trip through
  `forge::raw` without text conversion;
- a separate boundary test explicitly formats a returned signature through
  `forge::crypto::asymmetric::encoding`.

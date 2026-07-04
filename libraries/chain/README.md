# forge_chain

`forge_chain` owns neutral chain value types, wire-compatible records and
deterministic signing rules. It is a protocol primitive library, not a node,
controller, state database, consensus engine or product runtime layer.

## When To Use

- Build or inspect chain transactions, blocks, actions and ABI payloads.
- Need FC/Antelope-compatible raw byte layout for protocol records.
- Need deterministic transaction ids, block ids, signing preimages or digests.
- Need system action payload records such as `setcode`, `setabi` or `newaccount`.

## When Not To Use

- Do not put controller, state, execution, consensus, P2P sync or node lifecycle
  here.
- Do not store private keys or key custody policy here. Use `forge_crypto` or a
  signer plugin at the runtime boundary.
- Do not encode product account names, core symbols or chain-specific runtime
  policy in this library.
- Do not use text or JSON as a signing representation. Use `forge::raw::pack`
  and the chain helpers that define the protocol preimage.

## Public Modules

- `forge.chain.types` - names, assets, symbols, keys, signatures, timestamps and
  common scalar protocol vocabulary.
- `forge.chain.authority` - permissions, waits and authority thresholds.
- `forge.chain.transaction` - actions, transactions, packed transactions,
  transaction ids and transaction signing digests.
- `forge.chain.block` - block headers, signed blocks, receipts, block digests
  and block ids.
- `forge.chain.abi` - ABI structs and optional extension-field compatibility.
- `forge.chain.system` - system action payload records and canonical action
  names.

Target: `forge_chain`.

Dependencies: `forge_raw`, `forge_variant`, `forge_crypto` and
`forge_compression`.

Examples that pack action payloads import `forge.raw.raw` directly.

Package component: `chain`.

## Examples

### Build A System Action

System action payloads are neutral. The product or runtime layer supplies the
account that hosts those actions.

```cpp
import forge.chain.system;
import forge.chain.transaction;
import forge.chain.types;
import forge.raw.raw;

auto system_account = forge::chain::make_name("mychain");
auto action = forge::chain::action{};
action.account = system_account;
action.name = forge::chain::setabi::get_name();
action.authorization = {};
action.data = forge::raw::pack(forge::chain::setabi{
   .account = forge::chain::make_name("alice"),
   .abi = {},
});
```

### Calculate A Transaction Digest

```cpp
import forge.chain.transaction;

auto id = forge::chain::calculate_transaction_id(transaction);
auto digest = forge::chain::signature_digest(chain_id, transaction);
```

### Work With Packed Transactions

```cpp
import forge.chain.transaction;

auto packed = forge::chain::packed_transaction{
   signed_transaction,
   forge::chain::packed_transaction::compression::none,
};

auto unpacked = packed.get_signed_transaction();
```

## Boundaries

- `forge_chain` owns payload field order and deterministic protocol helpers.
- Products own chain account names, producer schedules, permissions, authority
  resolution and runtime validation.
- `forge_chain` may expose public keys and signatures as protocol values, but it
  must not expose private-key aliases or custody APIs.
- Compression policy for chain objects belongs in this library; byte-level zlib
  mechanics belong to `forge_compression`.

## Tests

`test_forge_chain` covers raw byte fixtures, name encoding, asset text
conversion, ABI compatibility, transaction and block ids, packed transactions
and signature recovery cases.

`test_forge_package_chain_component` verifies that installed consumers can use
`find_package(Forge CONFIG REQUIRED COMPONENTS chain)` and import chain modules.

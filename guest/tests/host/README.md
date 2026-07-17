# Contract Test Host

`forge_contract_test_host` is the executable oracle for the Contract SDK host
interface. It is a test-only library in namespace `forge::contract::testing`
and is neither installed nor exported as a Forge component.

## Responsibilities

- Register all 68 functions from the canonical intrinsic registry, including
  `current_receiver`.
- Execute a contract invocation with one `forge::db::object::transaction`.
- Commit successful `apply` and `eosio_exit`; roll back assertions, DB errors
  and VM traps.
- Implement Spring iterator and table behavior for the primary table and all
  five secondary-index families.
- Expose independent read helpers so tests inspect committed ObjectDB state
  without trusting the invocation-local iterator cache.

The seven schema models are direct `forge::db::object::object` values. Ranked
ObjectDB indexes own uniqueness, ordering, updates and rollback. The test-local
memory driver implements only ordered `forge::db::core` records; it has no
knowledge of Spring tables, secondary keys or iterators.

## Floating And 256-bit Keys

Double and long-double keys use the SoftFloat implementation already shipped
inside `forge.vm.wasm`. NaN is rejected before a DB query or mutation, and
signed zero maps to one ObjectDB sort key. `idx256` uses
`forge::chain::protocol::key256`; its legacy two-word pointer is handled by the
VM argument proxy so unaligned donor inputs are copied safely.

## Boundaries

This library is not a blockchain runtime. It does not implement RAM billing,
authorization management, controller policy, persistent schema migration or a
public memory database. Product host bindings must provide their own state
models and use production Forge DB drivers.

## Tests

`guest/tests/e2e/contract_tests.cpp` runs real generated wasm32 contracts
through `forge.vm.wasm`. The cases cover all 60 DB imports, C++23 `multi_index`
and `singleton`, primary and secondary traversal, transaction commit/rollback,
stale and wrong-kind iterators, payer and ownership checks, NaN, and unaligned
`idx256` input. Test-only deterministic snapshots compare the complete modern
and EOSIO ObjectDB result without exposing a public memory driver.

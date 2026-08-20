# Contract Testing

`forge_contract_testing` is the executable oracle for the Contract SDK host
interface. It is an installed Forge library in namespace
`forge::contract::testing` and is exported as the `contract_testing`
component.

## Usage

```cmake
find_package(Forge CONFIG REQUIRED COMPONENTS contract_testing)

add_executable(contract_state_tests contract_state_tests.cpp)
target_link_libraries(
   contract_state_tests
   PRIVATE Forge::forge_contract_testing
)
```

```cpp
import forge.contract.testing.host;
import forge.contract.testing.state;

auto host = forge::contract::testing::host{};
host.configure(forge::contract::testing::oracle_state{});
```

## Responsibilities

- Register all 152 functions from the canonical intrinsic registry without a
  second hand-written registration list.
- Execute a contract invocation with one `forge::db::object::transaction`.
- Commit successful `apply` and `eosio_exit`; roll back assertions, DB errors
  and VM traps.
- Implement Spring iterator and table behavior for the primary table and all
  five secondary-index families.
- Expose independent read helpers so tests inspect committed ObjectDB state
  without trusting the invocation-local iterator cache.
- Provide deterministic invocation state for authorization, accounts,
  transactions, producers, protocol features, resource limits and finality.
- Execute hashing, key recovery, extended crypto, BLS, synchronous calls,
  console output, inline actions and deferred transactions through real host
  callbacks.

The seven schema models are direct `forge::db::object::object` values. Ranked
ObjectDB indexes own uniqueness, ordering, updates and rollback. The test-local
memory driver implements only ordered `forge::db::core` records; it has no
knowledge of Spring tables, secondary keys or iterators.

## Floating And 256-bit Keys

Double and long-double keys use the SoftFloat implementation already shipped
inside `forge.vm.wasm.interpret`. NaN is rejected before a DB query or mutation, and
signed zero maps to one ObjectDB sort key. `idx256` uses
`forge::chain::protocol::key256`; its legacy two-word pointer is handled by the
VM argument proxy so unaligned donor inputs are copied safely.

## Boundaries

This library is not a blockchain runtime. Its authorization and privileged
state are deterministic fixtures, not a controller or consensus model. It does
not implement RAM billing, fork choice, controller lifecycle, persistent schema
migration or a public memory database. Product host bindings must provide their
own policy, state models and production Forge DB drivers.

## Tests

`guest/tests/e2e/contract_tests.cpp` runs real generated wasm32 contracts
through `forge.vm.wasm.interpret`. The database cases cover all 60 DB imports, C++23
`multi_index` and `singleton`, traversal, transaction commit/rollback, iterator
errors, payer and ownership checks, NaN, and unaligned `idx256` input. The full
oracle contract executes every non-database capability family, verifies
observable side effects and proves that assertion failures roll back ObjectDB
and fixture state together. A malformed-pointer case verifies that guest ranges
are rejected before a host callback touches memory.

# EOSIO Compatibility Veneer

`forge_guest_eosio` is a header-only compatibility surface over
`forge_guest_contract`. `eosio::contract` adapts `forge::contract::context`, value
types reuse `forge::chain::protocol`, and contract checks and action decoding
reuse the Forge modules. The contract-owned asset compatibility type preserves
the donor `.symbol` member spelling while delegating wire encoding and arithmetic
to the canonical protocol type.

```cpp
#include <eosio/eosio.hpp>

class [[eosio::contract("hello")]] hello : public eosio::contract {
 public:
   using contract::contract;
   [[eosio::action]] void greet(eosio::name user);
};

EOSIO_DISPATCH(hello, (greet))
```

`EOSIO_DISPATCH` is a macro-only adapter that selects an action and invokes
`forge::contract::execute_action`; it does not own a second decoder or runtime.
The veneer must not acquire its own allocator, codec, dispatcher implementation
or chain types. `<eosio/multi_index.hpp>`, `<eosio/singleton.hpp>` and
`<eosio/fixed_bytes.hpp>` are targeted aliases over the production Forge
templates and shared `key256`; they contain no second DB implementation.

```cpp
#include <eosio/eosio.hpp>

struct [[eosio::table("items")]] item {
   std::uint64_t id = 0;
   std::uint64_t primary_key() const { return id; }
   EOSLIB_SERIALIZE(item, (id))
};

using items = eosio::multi_index<"items"_n, item>;
```

## Dependencies And Stability

The INTERFACE target depends only on `forge_guest_contract`; aliases point to
the same guest protocol values and dispatcher templates. The currently shipped
contract context, value aliases, checks and `EOSIO_DISPATCH` are compatibility
surfaces. Header coverage grows only with donor-backed tests.

The legacy consumer is compiled from the relocated SDK and executed beside the
modern contract in the same `forge.vm.wasm` E2E suite. ABI parity tests prove
that Forge and EOSIO attribute spellings, `multi_index`, `singleton`, return
values and committed ObjectDB state have the same observable result.

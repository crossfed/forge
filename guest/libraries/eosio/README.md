# EOSIO Compatibility Veneer

`forge_guest_eosio` is a header-only compatibility surface over
`forge_guest_contract`. `eosio::contract` adapts `forge::contract::base`, value
types alias `forge::chain::protocol`, and contract checks and action decoding
reuse the Forge modules.

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
or chain types. The full unchanged EOSIO header corpus and database APIs are a
later compatibility block.

## Dependencies And Stability

The INTERFACE target depends only on `forge_guest_contract`; aliases point to
the same guest protocol values and dispatcher templates. The currently shipped
base contract, value aliases, checks and `EOSIO_DISPATCH` are compatibility
surfaces. Header coverage grows only with donor-backed tests.

The legacy consumer is compiled from the relocated SDK and executed beside the
modern contract in the same `forge.vm.wasm` E2E suite. ABI parity tests prove
that Forge and EOSIO attribute spellings produce the same canonical ABI.

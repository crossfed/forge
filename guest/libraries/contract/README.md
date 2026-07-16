# Guest Contract

`forge_guest_contract` is the modern C++23 contract API for wasm32. The primary
module `forge.contract` owns `forge::contract::context` and the core contract API.
Focused modules are `forge.contract.intrinsics` and
`forge.contract.dispatcher`; there is no aggregate-only module.

```cpp
import forge.contract;

class [[forge::contract("hello")]] hello : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] std::uint32_t add(std::uint32_t a, std::uint32_t b) {
      return a + b;
   }
};
```

Arguments and non-void results use the shared guest lane of `forge.raw`.
Results are returned through `set_action_return_value`. `intrinsics.hpp` is the
only signature registry and generates modern imports, EOSIO C declarations,
the host binding skeleton and the approved-import manifest. Blockchain host
implementations do not live here.

## Dependencies And Boundary

The target depends only on `forge_guest_runtime`, the guest lanes of
`forge.raw` and `forge.chain.protocol`, and the generated intrinsic C header.
It owns no host binding implementation, filesystem, threads, deployment logic
or blockchain state API.

## Stability And Tests

Intrinsic interface v1 and raw action/result bytes are compatibility contracts;
the C++23 API is experimental until the first SDK release. Modern void and
non-void actions, malformed input, return values, checks, package relocation
and execution in `forge.vm.wasm` are tested end to end.

# Spring Transaction Golden Provenance

The `builder_matches_spring_transaction_golden` fixture was generated with the
Spring serializer at commit
`e6a99f68b67abc4d89fe716755b2e1394a4991f7`.

The donor field order comes from these Spring files at that revision:

- `libraries/chain/include/eosio/chain/transaction.hpp`:
  `FC_REFLECT(transaction_header, ...)` and
  `FC_REFLECT_DERIVED(transaction, ...)`;
- `libraries/chain/include/eosio/chain/action.hpp`:
  `FC_REFLECT(action_base, ...)` and `FC_REFLECT_DERIVED(action, ...)`;
- `libraries/libfc/include/fc/io/raw.hpp`: `fc::raw::pack`.

[`donors/spring_transaction_golden.cpp`](donors/spring_transaction_golden.cpp)
constructs the same transaction as the Forge test and prints the result of the
donor's `fc::raw::pack(transaction)` as lowercase hexadecimal. Build that file
in a disposable pinned Spring checkout with this temporary target so it
inherits Spring's FC and Boost profile:

```cmake
add_executable(spring_transaction_golden /path/to/spring_transaction_golden.cpp)
target_link_libraries(spring_transaction_golden PRIVATE eosio_chain)
```

Build only `spring_transaction_golden`. Running it must print exactly:

```text
010000000100ddccbbaa00000000010000000000ea305500000000b863b2c20002010200
```

To refresh the fixture:

1. Check out the pinned Spring revision or record the new revision explicitly.
2. Add the donor generator to a temporary executable in the Spring build and
   link it to `eosio_chain`.
3. Build only that executable and capture its single output line.
4. Compare the output to the Forge test before changing the constant.
5. If bytes differ, treat the change as a wire-compatibility investigation;
   do not update the golden merely to make the test pass.

The generator is provenance material and is intentionally not part of the
Forge test target. CI verifies Forge against the pinned donor output without
requiring Spring as a build dependency.

The output above was reproduced on 2026-08-12 from the pinned Spring checkout
using Spring's `transaction.cpp` compile profile and built FC library. The
generated line matched the pre-existing Forge wire bytes exactly.

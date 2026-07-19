# Contract Attributes

Target `forge_contract_attributes`, package component `contract_attributes`,
owns the Clang `ParsedAttrInfo` registrations for contract, action, table and
synchronous-call annotations. Module `forge.contract.attributes.registry`
exports `forge::contract::attributes::register_all()`.

```cpp
import forge.contract.attributes.registry;

forge::contract::attributes::register_all();
```

Both `[[forge::...]]` and `[[eosio::...]]` spellings lower to the same canonical
annotations. This host-only library depends on compatible Clang/LLVM and does
not contain ABI generation, guest declarations or a plugin entry point. The
`attr-plugin` program is the thin loadable entry point.

## Dependencies

The library links only the compatible `clang-cpp` and `LLVM` surfaces needed by
`clang::ParsedAttrInfo`. Requesting package component `contract_attributes` is
what activates Clang discovery; unrelated Forge components remain Clang-free.

## Stability And Tests

Canonical annotation payloads are stable compatibility data. The C++ registry
entry point remains experimental until the first Contract SDK release. Tests
load all Forge and EOSIO spellings, compare their ABI output and build the
relocated loadable plugin through the package component.

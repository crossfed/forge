# Contract Attributes

Target `forge_tooling_attributes`, package component `tooling_attributes`,
owns the Clang `ParsedAttrInfo` registrations for contract, action, table and
synchronous-call annotations. Module `forge.tooling.attributes.registry`
exports `forge::tooling::attributes::register_all()`.

```cpp
import forge.tooling.attributes.registry;

forge::tooling::attributes::register_all();
```

Both `[[forge::...]]` and `[[eosio::...]]` spellings lower to the same canonical
annotations. This host-only library depends on compatible Clang/LLVM and does
not contain ABI generation, guest declarations or a plugin entry point. The
`attr-plugin` program is the thin loadable entry point.

## Dependencies

The library links only the compatible `clang-cpp` and `LLVM` surfaces needed by
`clang::ParsedAttrInfo`. Requesting package component `tooling_attributes` is
what activates Clang discovery; unrelated Forge components remain Clang-free.

## Stability And Tests

Canonical annotation payloads are Stable compatibility data. The C++ registry
entry point is Experimental in Forge 8.16.0. Tests load all Forge and EOSIO
spellings, compare their ABI output and build the relocated loadable plugin
through the package component.

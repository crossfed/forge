# abigen

`abigen` generates a chain ABI and dispatcher from annotated contract sources.
It is a thin process entry point over `forge_contract_abi`.

```bash
abigen --contract hello --abi hello.abi --dispatch hello.dispatcher.cpp \
  --attribute-plugin ./attr-plugin --sysroot ./sysroot hello.cpp
```

Multi-source build integration may pass one generated output for every helper
translation unit:

```bash
abigen --contract token --abi token.abi --dispatch generated/token.dispatcher.cpp \
  --source-wrapper generated/token.source-1.cpp \
  --attribute-plugin ./attr-plugin --sysroot ./sysroot token.cpp transfer.cpp
```

The first source is the dispatch source. Wrapper paths correspond in order to
the remaining sources; normal contract builds should use
`forge_add_contract(... DISPATCH_SOURCE ...)`, which manages this mapping.

Exit code `0` means both artifacts were generated; `1` reports a diagnostic
from the owning library. See `libraries/contract/abi/README.md` for the API.

Inputs are one or more annotated C++ sources, contract name, wasm32 sysroot,
attribute plugin and optional Ricardian files. Outputs are the canonical `.abi`,
generated dispatcher and optional helper wrappers; the program stores no
compiler or ABI state.

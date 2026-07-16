# abigen

`abigen` generates a chain ABI and dispatcher from annotated contract sources.
It is a thin process entry point over `forge_contract_abi`.

```bash
abigen --contract hello --abi hello.abi --dispatch hello.dispatcher.cpp \
  --attribute-plugin ./attr-plugin --sysroot ./sysroot hello.cpp
```

Exit code `0` means both artifacts were generated; `1` reports a diagnostic
from the owning library. See `libraries/contract/abi/README.md` for the API.

Inputs are one or more annotated C++ sources, contract name, wasm32 sysroot,
attribute plugin and optional Ricardian files. Outputs are the canonical `.abi`
and generated dispatcher `.cpp`; the program stores no compiler or ABI state.

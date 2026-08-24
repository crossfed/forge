# contract-check

`contract-check` validates a built WASM/ABI pair against the approved intrinsic
manifest.

```bash
contract-check --wasm hello.wasm --abi hello.abi \
  --imports intrinsics.json --required-export apply
```

Exit code `0` means validation succeeded; `1` prints the typed validation
diagnostic. Validation is implemented by `forge_tooling_validation`.

Inputs are a WASM module, canonical ABI, approved intrinsic manifest and
required export name. The command is read-only and produces no artifact.

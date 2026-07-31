# contract-manifest

`contract-manifest` creates the deterministic sidecar for a validated contract.

```bash
contract-manifest --wasm hello.wasm --abi hello.abi --imports intrinsics.json \
  --output hello.contract.json --sdk-version 8.5.0 --profile developer \
  --reproducible false --llvm-version llvmorg-22.1.8 --llvm-commit COMMIT \
  --sysroot-version 1 --sysroot-hash HASH --intrinsic-version 1
```

Exit code `0` writes the manifest; `1` reports an error from
`forge_contract_manifest`. The program contains no manifest domain model.

Inputs are the validated WASM/ABI pair, approved imports and explicit toolchain
identity. The only output is the deterministic `*.contract.json` sidecar.

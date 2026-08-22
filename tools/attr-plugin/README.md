# attr-plugin

`attr-plugin` is the loadable Clang entry point for Forge/EOSIO contract
attributes. Loading the module calls
`forge::tooling::attributes::register_all()`; all parsing and annotation logic
lives in `forge_tooling_attributes`.

```bash
clang++ -fplugin=./attr-plugin contract.cpp
```

The shared module has no standalone CLI, output artifact or process exit-code
contract: Clang reports plugin loading and attribute diagnostics through its own
exit status. The program owns no ABI schema or AST visitors beyond plugin
activation.

Input is the Clang compilation being processed. Output is canonical
`clang::annotate` metadata in that AST; the module does not write files.

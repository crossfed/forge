# Contract Compatibility Corpus

This directory contains immutable source fixtures used to prove the Forge
Contract SDK against pinned Spring, CDT and legacy EOSIO releases. Files under
`spring/` and `eosio/` are copied byte-for-byte from their donors. Build files,
test drivers and generated evidence live outside those donor trees.

Pinned donors and the source path and SHA-256 of every file are recorded in
`provenance.json`. Run the integrity gate with:

```bash
python3 check.py integrity --root .
```

Never format or repair donor source here. Compatibility fixes belong to the
Forge SDK, EOSIO veneer, compiler tooling or executable test host. A donor bug
may be documented, but its fixture remains unchanged.

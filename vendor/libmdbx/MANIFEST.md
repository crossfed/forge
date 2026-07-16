# libmdbx v0.14.2 Vendor Manifest

Forge vendors the official libmdbx amalgamated C release as a private backend.
The public Forge API does not expose `MDBX_*` or `mdbx_*` declarations.

## Source Pin

- Version: `0.14.2`
- Annotated tag: `v0.14.2`
- Tag object: `a944aa0dfe558f4decc0a50f27d8b3c1ba1da9d5`
- Tag commit: `c8780aecc3dea7f4f2cb83e88bb4d33a622774bd`
- Commit tree: `1331daffff275805a4d225deb86acb6c7c5d47a8`
- Release archive: `https://github.com/erthink/libmdbx/archive/refs/tags/v0.14.2.tar.gz`
- Archive SHA-256: `e7246cc363d1e23eb373112b9957190beef22bb7ab202cb1430dd90b08bbeea4`

GitHub is an upstream-published mirror, not the primary repository. The
upstream explanation is preserved verbatim in `NOTICE`.

## Imported Files

The following files are copied without modification from the tag commit:

| File | SHA-256 |
| --- | --- |
| `mdbx.c` | `ec7eb24300237e79020c3c0a9345ac0f259f1da448d391b5afcbf82bf478b58c` |
| `mdbx.h` | `f40db8d625cd96787a4e1bba53f05dd8f2107c8615ad4c37a3e89677b69ac988` |
| `mdbx-internals.h` | `892eb3577ae103048b17946efc904dbea9fc897f9eecf52463ee68d86455de57` |
| `VERSION.json` | `dcd90435b2a91475d2b8fb89d795e5fac356b9befd737578552e3200c2ad81ca` |
| `LICENSE` | `0d542e0c8804e39aa7f37eb00da5a762149dc682d7829451287e11b938e94594` |
| `NOTICE` | `1596e2db4124828fe46a8ad356d5eb03d80ff15ffe93bc4891a383f802ef0c48` |
| `COPYRIGHT` | `7fd2531b9f19d6e34716fa9f636721f743f8baf5f3f3ed5c2e3ba2ff8416da10` |

Upstream tools, their `mdbx-wingetopt.h` support header, the C++ wrapper and
upstream build scripts are intentionally not imported. Forge compiles only
`mdbx.c`; its upstream private declarations remain in the unmodified
`mdbx-internals.h`. The build timestamp is deterministic and derived from the
release commit time.

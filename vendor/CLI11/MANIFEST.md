# CLI11 v2.6.2 Vendor Manifest

Forge vendors the official CLI11 header set as a private parser backend for
`forge_cli`. CLI11 types, exceptions and formatters are not part of the Forge
public API.

## Source Pin

- Version: `2.6.2`
- Tag: `v2.6.2`
- Tag commit: `37bb6edc5317e99af72ef48405e65d9ca5218861`
- Release archive: `https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.6.2.tar.gz`
- Archive SHA-256: `c6ea6b2e5608b3ea8617999bd5f47420c71b2ebdb8dc4767c1034d1da5785711`

## Imported Files

The upstream `include/CLI` tree and `LICENSE` are copied without modification
from the release archive. Forge does not import CLI11 examples, tests, build
scripts, documentation or package metadata. Only `parser.cpp` includes
`CLI/CLI.hpp`, through a target-private system include directory.

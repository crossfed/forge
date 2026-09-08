# c-ares 1.34.8 Vendor Manifest

Forge vendors the exact official c-ares `1.34.8` release tree as the private,
static asynchronous DNS backend for `forge_net_dns`. The c-ares C API is never
installed as a Forge public interface and is not a Forge package dependency.

## Source Pin

- Release: `1.34.8`
- Tag: `v1.34.8`
- Archive: `https://github.com/c-ares/c-ares/releases/download/v1.34.8/c-ares-1.34.8.tar.gz`
- Archive SHA-256: `c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`
- Detached signature: `https://github.com/c-ares/c-ares/releases/download/v1.34.8/c-ares-1.34.8.tar.gz.asc`
- Signing key primary fingerprint: `DA7D64E4C82C6294CB73A20E22E3D13B5411B7CA`
- Signing subkey fingerprint: `75EB6CA0E63E90C4FF2C868FC1D15611B2E4720B`

The archive SHA-256 matches c-ares release metadata. Its detached signature
verifies with the listed signing subkey, whose primary fingerprint is
published on `https://c-ares.org/download/`.

## Complete Release Tree

`TREE.SHA256` lists SHA-256 entries for all 532 upstream files. It excludes
this manifest and itself. Its SHA-256 is
`cc85467961f47f77e6867ff8359786262bde511df45380bbfcb48806ca331988`.

The 532 upstream files under `vendor/c-ares` are unmodified. `MANIFEST.md` and
`TREE.SHA256` are Forge metadata excluded from that release-tree checksum.
Forge integration lives outside the vendored source in the root CMake
registration and `libraries/net/dns`.

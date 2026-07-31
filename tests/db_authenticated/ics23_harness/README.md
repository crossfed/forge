# Official ICS23 vector harness

This isolated Go module verifies copied upstream IAVL vectors with the official
`github.com/cosmos/ics23/go` implementation. It is not linked into Forge and it
does not assert that Forge authenticated proof schema v2 is ICS23-compatible.

The verifier dependency is pinned to upstream commit
`7f2c2d0965fdcf33658cce3198ddae078a449fc2` through its Go pseudo-version.

Upstream source: `cosmos/ics23` tag `v0.7.1`, commit
`014bd93b66bb57e5f250be0c9a344505f7d0fa70`:

- `testdata/iavl/exist_middle.json`
- `testdata/iavl/nonexist_middle.json`

The copied fixtures retain their upstream Apache-2.0 provenance.

Run this harness explicitly with `go test ./...` from this directory when the
official cross-language boundary is being validated. The normal Forge CMake
build neither downloads nor builds Go/protobuf dependencies.

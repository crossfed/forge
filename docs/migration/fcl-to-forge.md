# FCL 5.x to Forge Migration

Forge is the new public name for FCL. This is an intentional major-breaking
rename with no compatibility aliases.

## Mechanical Mapping

| Old FCL spelling | New Forge spelling |
| --- | --- |
| `namespace fcl` | `namespace forge` |
| `fcl::api` | `forge::api` |
| `import fcl.api.binding` | `import forge.api.core.binding` |
| `#include <fcl/api/macros.hpp>` | `#include <forge/api/core/macros.hpp>` |
| `FCL_API` | `FORGE_API` |
| `FCL_API_METHOD` | `FORGE_API_METHOD` |
| `FCL_HTTP_API` | `FORGE_HTTP_API` |
| `FCL_THROW_EXCEPTION` | `FORGE_THROW_EXCEPTION` |
| `FCL_ASSERT` | `FORGE_ASSERT` |
| `fcl_core` | `forge_core` |
| `fcl_api_http` | `forge_api_http` |
| `fcl_api_transport` | `forge_api_transport` |
| `fcl_plugins_crypto_signer` | `forge_plugins_crypto_signer` |
| `FCL::fcl_core` | `Forge::forge_core` |
| `find_package(FCL CONFIG REQUIRED)` | `find_package(Forge CONFIG REQUIRED)` |
| `fcl.raw` | `forge.raw` |
| `fcl.typed` | `forge.typed` |
| `fcl.plugins.*` | `forge.plugins.*` |
| `FCL_ENABLE_*` | `FORGE_ENABLE_*` |
| `FCL_HAS_*` | `FORGE_HAS_*` |
| `FCL_PACKAGE_*` | `FORGE_PACKAGE_*` |

## What Did Not Change

Plugin config sections that were already product-neutral keep their names:

```yaml
plugins:
  crypto:
    signer:
      # ...
```

Examples include `plugins.crypto.signer`, `plugins.crypto.secrets`,
`plugins.http.server`, and `plugins.p2p.node`.

## No Compatibility Aliases

Forge does not ship compatibility aliases for old FCL names:

- no `namespace fcl = forge`;
- no `FCL_*` macro forwarding;
- no `FCL::` exported CMake namespace;
- no `FCLConfig.cmake`;
- no acceptance of old `fcl.*` codec or plugin identity strings.

Consumers should update source, CMake targets, module imports, includes, macros,
and runtime identity assertions in one migration pass.

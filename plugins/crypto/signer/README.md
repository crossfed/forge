# Signing Provider Plugin

`forge::plugins::crypto::signer` publishes a local-only typed API for signing
digests with configured private keys. The API returns Forge Crypto binary value
types; text encoding belongs to the consuming boundary.

## When To Use

- A Forge application needs local signing through a plugin-owned API.
- Keys are configured locally and selected by `key_id` and purpose.
- Callers already have a digest and need a typed signature result.

## When Not To Use

- Do not use this plugin as a wallet, remote KMS or authorization service.
- Do not pass raw product DTO strings for signing. Pack/hash the DTO in the
  consuming layer, then pass a digest.
- Do not expose private keys through generated examples, CLI flags or logs.

## Identity

- Target: `forge_plugins_crypto_signer`
- Package component: `plugins_crypto_signer`
- Plugin id: `forge.plugins.crypto.signer`
- Main API id: `forge.plugins.crypto.signer`
- Config section: `plugins.crypto.signer`
- Public modules:
  - `forge.plugins.crypto.signer.plugin`
  - `forge.plugins.crypto.signer.api`
  - `forge.plugins.crypto.signer.types`
  - `forge.plugins.crypto.signer.exceptions`

## What It Provides

- Loads configured local private keys through `forge_crypto`.
- Enforces key ids, allowed purposes and optional required algorithms.
- Signs `forge::crypto::sha256` digests through a local-only `forge_api_core` contract.
- Returns `forge::crypto::asymmetric::public_key` and
  `forge::crypto::asymmetric::signature` directly.
- Keeps key material config secret/redacted through schema/config metadata.

It is not a wallet, vault, hardware security module or authorization layer. It
does not decide what a payload means; it only signs allowed digests with
configured keys.

## Dependencies

- `forge_app`
- `forge_api_core`
- `forge_crypto`
- `forge_config_core`
- `forge_schema`

## Config

```yaml
plugins:
   crypto:
      signer:
         keys:
            - id: service-key
              private-key: "<redacted private key>"
              input-profile: forge
              purposes: ["api.receipt"]
```

`keys` is a secret object-list field. Load it from a protected config source;
do not rely on generated CLI or environment options for key material.
`input-profile` is used only while parsing configured private-key text.

## Examples

```cpp
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.plugin;
import forge.crypto.asymmetric;
import forge.raw.raw;

auto signer = context.apis().get<forge::plugins::crypto::signer::api>(
   {.id = {"forge.plugins.crypto.signer"}, .major = 2});

auto result = co_await signer->sign(
   forge::plugins::crypto::signer::request{
      .key_id = "service-key",
      .purpose = "api.receipt",
      .digest = digest,
      .required_algorithm =
         forge::crypto::asymmetric::algorithm::secp256k1,
   });

auto bytes = forge::raw::pack(result.signature);

// Text is an explicit external-boundary concern.
auto text = forge::crypto::asymmetric::encoding::antelope().format(
   result.signature);
```

```cpp
registry.register_plugin(forge::plugins::crypto::signer::descriptor());
```

## Security And Boundaries

- Private key material is schema-marked secret and must be loaded from protected
  config sources.
- The plugin signs allowed digests only; it does not decide whether a payload is
  authorized.
- Purpose checks are plugin-local allow-lists. Product policy must define what a
  purpose means.
- Raw serialization preserves the existing Forge Crypto binary layouts. The
  signer contract does not select or persist a text profile.

## Common Mistakes

- Signing JSON strings or manually concatenated fields. Prefer
  `Boost.Describe -> forge::raw::pack -> hash -> sign`.
- Reusing one key id for unrelated purposes without an explicit purpose list.
- Logging request structs that may contain key ids and operational context.
- Formatting signatures inside domain or protocol models. Apply
  `asymmetric::encoding` only at a text transport or configuration boundary.

## Tests

- `test_forge_plugins`
- `test_forge_package_plugins_crypto_signer`

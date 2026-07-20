# Unreleased

## Contract SDK Codec Migration

This is an explicitly approved pre-stabilization source and package break. The
overlapping Forge encoding modules were removed without compatibility aliases.
EOSIO contract headers remain source-compatible adapters.

| Removed module | Replacement |
| --- | --- |
| `forge.core.encoding` | `forge.codec.base64` or `forge.codec.hex` |
| `forge.crypto.base64` | `forge.codec.base64` |
| `forge.crypto.base58` | `forge.codec.base58` |
| `forge.crypto.hex` | `forge.codec.hex` |
| `forge.contract.base64` | `forge.codec.base64` |

The new targets and package components are `forge_codec_base64` /
`codec_base64`, `forge_codec_base58` / `codec_base58`, and `forge_codec_hex` /
`codec_hex`. Base64URL, padding, wrapping and whitespace handling are selected
through explicit options.

## Asymmetric Value Migration

This is an explicitly approved pre-stabilization source and persisted-value
break. Host and contract code now share one `forge.crypto.asymmetric.value`
model. `forge.chain.protocol` and `forge.contract` expose aliases to that model
rather than separate records or converters.

The canonical variant tags are:

| Tag | Algorithm |
| ---: | --- |
| `0` | secp256k1 (K1) |
| `1` | P-256 (R1) |
| `2` | WebAuthn (WA) |
| `3` | Ed25519 |
| `4` | RSA |

Spring-compatible K1, R1 and WebAuthn bytes remain unchanged. Previous host
Forge values used tag `2` for Ed25519 and tag `3` for RSA. Persisted or recorded
values containing those alternatives must be decoded with the previous Forge
version and packed again with the new canonical type; the new decoder does not
guess which historical schema produced an ambiguous tag.

Source migration is mechanical:

| Removed usage | Replacement |
| --- | --- |
| `key.verify(message, signature)` | `forge::crypto::asymmetric::verify(key, message, signature)` |
| `public_key(signature, digest)` | `forge::crypto::asymmetric::recover(signature, digest)` |
| `value.type()` | `forge::crypto::asymmetric::type(value)` |
| `value.as<T>()` | `std::get<T>(value)` |
| `value.to_string()` | `forge::crypto::asymmetric::encoding::forge().format(value)` |

The removed backend `*_shim` types have no aliases. `private_key` remains
host-only and stores the actual backend private-key classes.

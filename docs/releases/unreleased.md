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

# Forge Crypto PKI

`forge_crypto_pki` owns host public-key infrastructure codecs and certificate
inspection. Package component: `crypto_pki`. Public namespace:
`forge::crypto::pki`.

## Modules

- `forge.crypto.pki.der`
- `forge.crypto.pki.pem`
- `forge.crypto.pki.x509`

```cpp
import forge.crypto.pki.x509;

const auto certificate =
   forge::crypto::pki::x509::certificate::from_pem(pem_text);
const auto key = certificate.key();
```

The target depends on Crypto Core, Digest, Asymmetric, Codec Hex, Exceptions
and OpenSSL Crypto. It does not manage trust policy, secret custody or network
authentication. `test_forge_crypto_pki` and the package consumer verify key and
certificate boundaries.

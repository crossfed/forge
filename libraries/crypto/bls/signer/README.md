# forge.crypto.bls.signer

`forge.crypto.bls.signer` provides a transport-neutral, local BLS signing
provider. It is suitable for a local key source, HSM adapter, or KMS adapter
that exposes one finalizer identity. It owns neither a plugin lifecycle nor an
authorization policy.

## Public Modules

- `forge.crypto.bls.signer.provider`
- `forge.crypto.bls.signer.configured_provider`

`configured_provider` receives a BLS private key from its embedding process and
returns the corresponding public key, proof of possession, and typed BLS
signature. It does not parse configuration, read the environment, expose a
listener, or log key material.

```cpp
#include <utility>

import forge.crypto.bls;
import forge.crypto.bls.signer.configured_provider;

auto provider = forge::crypto::bls::signer::configured_provider::create(std::move(key));
auto identity = co_await provider->describe();
auto signed_value = co_await provider->sign(canonical_finality_message);
```

The caller owns canonical message construction, domain separation, cancellation
policy, and authorization. Do not expose this provider directly through a
remote API that accepts arbitrary bytes.

## Target

`forge_crypto_bls_signer`

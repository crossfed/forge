# forge.api.auth

`forge.api.auth` carries a caller identity established by a server-side transport
binding. It is not an identity provider, session store, authorization engine, or
transport listener.

## Public Module

- `forge.api.auth.authenticated_caller`

## Transport Boundary

`authenticated_caller` is a `forge::api::core::server_supplied` value. A remote
client can serialize the field only as an ignored placeholder: the server resets
it before handler dispatch and replaces it from `trusted_invocation`. A binding
must add the value only after it has authenticated the transport principal.

```cpp
import forge.api.auth.authenticated_caller;
import forge.api.core.trusted_invocation;

auto trusted = forge::api::core::trusted_invocation_builder{}
   .set(forge::api::auth::authenticated_caller{
      forge::api::auth::caller_source::tls_certificate,
      certificate_fingerprint,
   })
   .build();
```

Consumers compare the typed source and certificate/peer fingerprint using their
own exact authorization policy. Do not infer authorization from the existence
of an authenticated caller, accept client-supplied identity material, or place
credentials in this value.

## Target

`forge_api_auth`

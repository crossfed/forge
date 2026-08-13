# forge.crypto.signer

Transport-neutral signing provider interface for local keystores, hardware
devices, HSM/KMS adapters and product integrations. The library does not own a
key container or a node plugin lifecycle.

`forge.crypto.signer.configured_provider` adapts exactly one explicitly selected
private key to `provider`. The application obtains the source from Forge Config
and chooses either an inline `secret_string` or a file path; the provider never
reads environment variables. File input is bounded, rejects symlinks, and on
POSIX requires a regular file owned by the current user with exact mode `0400`.
Source text and parse failures are never included in exception context.
`configured_provider::create` rejects options containing neither source or both
sources, so application config cannot select one ambiguously.

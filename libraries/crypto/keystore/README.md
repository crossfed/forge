# forge.crypto.keystore

Encrypted local-file signing provider for one-shot clients. The file container
uses bounded scrypt and AES-256-GCM with authenticated metadata. Writes are
atomic and use private owner-only filesystem permissions.

Every encryption generates its salt and GCM nonce internally; callers cannot
override either value. A post-publication directory-sync failure raises the
typed `durability_unknown` exception. The live store already reflects the
published file in that case, but the caller must treat its survival across an
immediate system crash as unknown.

The container follows security patterns from ERC-2335 and age, but it is a
Forge format and does not copy either project's chain-specific representation.

`forge.crypto.keystore.password` is the only supported boundary for CLI
secrets. It reads a bounded password from a hidden interactive terminal,
standard input or an owner-only regular file. Callers pass only a password-file
path or the decision to consume stdin through argv; the password itself is
never a command argument or an ordinary config option.

Decrypt validates the complete container shape before running scrypt or AES:
the format has a 16-byte salt, a 12-byte GCM nonce, a 16-byte GCM tag and an
exact ciphertext length. Scrypt `N` must be a power of two and all KDF values
must stay inside caller-provided limits. Malformed shape and KDF values fail as
typed `exceptions::invalid_file`; valid wire bytes are unchanged.

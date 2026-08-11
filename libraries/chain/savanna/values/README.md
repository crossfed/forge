# Forge Chain Savanna Values

`forge_chain_savanna_values` owns the guest-safe finalizer and finalizer-policy
records shared by Chain Protocol, the host Savanna kernel, and contracts.

The component contains values and serialization only. BLS validation, proof of
possession, quorum certificates, and finality operations remain host-only.

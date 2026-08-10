# Forge CLI Client Foundation v1

## Goal

Provide a neutral command-line application foundation for one-shot clients
without turning config parsing, terminal UI or product commands into one
framework.

The same command mechanics must be usable by chain clients, developer tools and
ordinary Forge applications. Downstream product vocabulary does not enter the
public API.

## Parser Ownership

`forge_config_program_options` remains the argv adapter for configuration
documents. It uses Boost.Program_options privately and is the correct surface
for daemon flags, config-source precedence and schema-generated config help. It
does not become a nested command framework.

A new `forge_cli` leaf owns hierarchical command grammar:

- nested commands and required command paths;
- positional arguments, flags, aliases and typed option conversion;
- command-local and inherited options;
- command callbacks and deterministic dispatch;
- contextual `--help`, `--help-all` and parse diagnostics;
- shell-completion metadata derived from the same command description.

CLI11 is the private parser backend. CLI11 types, exceptions and formatter
objects must not appear in public `.cppm` files. Public command descriptions,
selected-command values and failures are Forge-owned types. Backend failures
are translated to typed Forge CLI exceptions at the boundary.

`forge_cli` is not a terminal UI, config source adapter, application lifecycle,
network client, transaction builder, wallet or key store. Those remain separate
leaf libraries and may be composed by a consuming program.

## Donor

Spring `cleos` at commit `e6a99f68b67abc4d89fe716755b2e1394a4991f7`
is the command-coverage and terminal-UX donor.

Historical EOSIO commit `f40f189d3647d682f46a900c7fc42bfda93a4a48`
replaced the old manual `if/else` command parser with CLI11 `CLI::App`, nested
`add_subcommand`, typed options and callbacks. Commit
`e872980baf014a9e9ecdb757f773a3355e93be0f` later moved the existing CLI11
dependency to a submodule; it was not the initial parser decision.

Accepted donor patterns:

- first-class nested command ownership;
- required subcommands and command-local validation;
- callbacks after successful typed parsing;
- contextual help and a customizable formatter;
- one-shot process semantics.

Rejected donor patterns:

- a monolithic program `main.cpp` containing command behavior;
- direct CLI11 types in product code or public APIs;
- hidden wallet-daemon auto-start;
- mixed snake-case and kebab-case command names;
- arguments that ambiguously accept either JSON text or a filename;
- private keys and passwords in argv.

## Consumer Shape

A product program keeps `main.cpp` thin. Reusable command descriptions and
handlers live in product-owned libraries; the program composes them with Forge
CLI and explicitly provides any async runtime, API clients, output formatters
and signing providers it needs.

Command behavior and authorization remain downstream-owned. A future async
application host may adapt handlers returning `boost::asio::awaitable<int>`
without putting network or runtime ownership into the parser.

## Follow-On Foundations

The client foundation requires separate Forge work after command routing:

1. transaction preparation: expiration, TAPOS, ABI action packing, required
   keys and canonical signing payloads;
2. encrypted local keystore and a user-key signer provider;
3. composition with existing Chain API `raw_client`, `verified_client` and
   `submission_client`.

The existing `forge.plugins.crypto.signer` remains a node runtime signing
provider. It is not a wallet, vault or encrypted user keystore.

## Acceptance

- nested command selection and required-subcommand failures;
- positional arguments, aliases, repeated options and typed conversion;
- inherited global options and command-local conflicts;
- stable contextual help and completion metadata;
- unknown, missing and malformed argument diagnostics;
- callback execution only after complete successful parsing;
- CLI11 exceptions translated to typed Forge exceptions;
- no CLI11 or Boost.Program_options types in public module interfaces;
- installed package consumer defining and dispatching a nested command tree;
- structure, formatting and `git diff --check` gates.


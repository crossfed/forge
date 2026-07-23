# Forge Chain Remote State v1

Status: accepted architecture direction. This document does not describe a
shipped API, target or package component.

Related boundaries:

- [Forge Contract SDK Toolchain v1](forge-contract-sdk-toolchain-v1.md);
- [Forge Chain Remote State Donor Baseline](../donors/forge-chain-remote-state-v1.md);
- [API Core](../../libraries/api/core/README.md);
- [Chain family](../../libraries/chain/README.md);
- [DB Core](../../libraries/db/core/README.md).

## Purpose

FORGE needs one product-neutral way for host applications to read typed
contract state and submit chain transactions through HTTP, P2P, QUIC,
WebSocket or future API transports.

This is not a remote DB Core backend. The feature is a Chain-owned service
contract and typed client built on `forge_api_core`.

The design preserves four distinct responsibilities:

1. a downstream contract library owns shared row, action and schema
   declarations;
2. the guest Contract SDK owns intrinsics, authorization, state mutation and
   dispatch;
3. the Chain remote-state layer owns chain consistency, typed table queries and
   transaction-submission semantics;
4. Forge API owns method descriptors, dispatch, proxies and transport binding.

## Decision

DB-shaped reads are useful, but remote chain access must not implement
`forge::db::core::driver`, `forge::db::object::store` or their transaction
contracts.

DB Core promises backend transactions, commit/rollback, stable local snapshots,
savepoints, record families and participant hooks. A blockchain exposes a
different contract:

- accepted transactions may not yet be irreversible;
- a fork may replace head state;
- callers cannot roll back committed chain state;
- state mutation must pass contract authorization and validation;
- paged reads are consistent only when tied to an explicit chain-state anchor.

Consequently:

- reads may expose typed table and index views that feel database-like;
- writes remain actions assembled into signed chain transactions;
- submission acknowledgement, execution and irreversible finality remain
  distinct states;
- no API may present transaction submission as `db.commit()`.

The node may use FORGE DB internally. That implementation choice is private to
the node and does not change the remote contract.

## Layering

```text
downstream shared contract schema
   +-- guest adapter -> multi_index/intrinsics -> WASM
   `-- host adapter  -> typed Chain state views
                              |
                    Chain state API contract
                              |
                    forge_api_core proxy/dispatch
                              |
             HTTP | P2P | QUIC | WebSocket | future
```

The Chain library must not import HTTP, P2P or another concrete transport.
Callers provide a connected `forge::api::core::remote_mount` or an equivalent
API proxy. Creating that mount owns transport-specific policy:

- URL, TLS, authentication and HTTP retries;
- peer discovery, peer selection and P2P protocol routing;
- QUIC connection establishment;
- connection failover and transport diagnostics.

The same Chain API descriptor and method set is registered once and may be
served over every compatible Forge API binding.

## Shared Contract Schema

A dual-target contract library is the single source for deterministic values
used by both host and guest:

- action payload and table-row types;
- canonical contract, action, table and index names;
- primary and secondary key types;
- pure key extraction and ordering declarations;
- Raw serialization shape;
- ABI or schema identity.

Table and index declarations must be shared metadata. If index declarations
exist only in guest code, a host client cannot offer type-safe secondary-index
queries or validate key encoding.

The shared schema does not expose guest storage mechanics. Guest-only code
adapts a table descriptor to `multi_index` and owns mutation, payer,
authorization and intrinsic calls. Host-only code adapts the same descriptor to
a remote read view and owns networking, retries, caches, signing and service
policy.

C++23 modules are the packaging mechanism, not the boundary itself. Shared
modules must remain compilable for both targets and must not conditionally leak
host networking or guest intrinsics into the other target.

## Remote Contracts

The intended service surface is split by responsibility instead of collected
in one unbounded RPC interface:

- chain information and capability discovery;
- read-only contract-state queries;
- transaction submission and status/finality observation.

Working names such as `forge_chain_state` and `forge_chain_client` describe
ownership but are not approved package commitments until implementation applies
the `create-library` rules.

The public API contract is exported once with `FORGE_EXPORT_API`. A server
plugin implements the abstract contract and installs its implementation in the
Forge API registry. A client obtains the generated proxy from a remote mount.
Plugins are expected integration points, not dependencies of the Chain
library.

The node-facing wire contract is generic. It accepts table descriptors and
encoded bounds rather than registering one Forge API for every downstream
contract. Typed host wrappers perform compile-time selection and Raw
encoding/decoding using the downstream shared schema.

Conceptually:

```cpp
auto remote = mount.get_remote_api<forge::chain::state::api>();
auto state = forge::chain::state::client{std::move(remote)};

auto read = co_await state.begin_read({
   .consistency = forge::chain::state::consistency::irreversible
});

auto uploads = read.table<example_contract::uploads>();
auto value = co_await uploads.find(upload_id);
auto page = co_await uploads
   .index<example_contract::by_owner>()
   .equal_range(owner, {.limit = 100});
```

The exact C++ shape remains an implementation design task.

## Consistency And Pagination

Every read must state the consistency it requires. The initial design must
distinguish at least:

- best-effort current head;
- current irreversible state;
- an explicit block identifier or number.

Resolving a symbolic selector returns a concrete state anchor. Every subsequent
page in that logical read uses the same anchor. Cursors are opaque, bounded and
tied to:

- chain identity;
- state anchor;
- contract, scope, table and index;
- encoded bounds and direction;
- schema or ABI identity.

A cursor used with another query, chain, block or schema fails with a typed
error. Responses report their observed state anchor. The API must not call a
view a snapshot when the backend cannot preserve that anchor.

The Spring-compatible `get_table_rows` adapter may honestly provide
best-effort-head pagination. Stronger repeatable reads require a node endpoint
backed by retained block state, state history or another explicit pinned-view
mechanism.

Queries are page-bounded. The remote contract does not expose unbounded
iterators whose lifetime implicitly holds server resources.

## Schema And Capability Validation

Connection or first-use negotiation must make these values available:

- chain identifier and protocol version;
- supported read consistency modes;
- historical-state and index capabilities;
- maximum page/request sizes;
- contract ABI or schema identity where available;
- supported transaction submission and finality observation modes.

Typed decoding must fail before returning a row when the requested shared
schema does not match the chain's active contract schema. Silent best-effort
decoding is forbidden.

The initial typed error surface must cover at least:

- unknown contract, table or index;
- unsupported consistency or historical read;
- unavailable or forked state anchor;
- stale or mismatched cursor;
- schema/ABI mismatch;
- malformed row or key bytes;
- rejected, expired or duplicate transaction;
- unavailable finality observation.

## Write Boundary

Host applications submit typed actions through chain transactions:

```cpp
auto tx = chain.transaction();
tx.action(example_contract::create_upload{...});

auto submitted = co_await chain.submit(std::move(tx));
co_await chain.wait(
   submitted.id,
   forge::chain::finality::irreversible);
```

The final API may separate building, signing, submission and observation into
focused libraries. Key custody remains outside the generic Chain state API.

No remote CRUD operation may write contract tables directly. Such an operation
would bypass contract authorization, deterministic execution, resource
accounting and consensus validation.

## Server And Plugin Integration

A blockchain node plugin provides concrete API implementations over the node's
controller/state services. It is responsible for:

- resolving consistency selectors to concrete anchors;
- validating chain, schema and query capabilities;
- executing bounded table/index queries at the anchor;
- accepting packed transactions through the node's normal validation path;
- publishing transaction status and finality evidence.

The plugin must not export controller, DB backend or donor-specific types in the
public API. The generic Chain library remains usable by non-plugin clients and
servers.

## Non-Goals

This direction does not introduce:

- a remote `forge.db` driver;
- remote DB savepoints, rollback or arbitrary table mutation;
- Spring-specific JSON DTOs as canonical Forge types;
- one generated API contract per downstream WASM contract;
- server-held unbounded iterators or snapshots;
- consensus, fork-choice or controller implementation in Forge Chain;
- transport selection inside the Chain state library;
- product-specific cache, retry, authorization or billing policy.

## Delivery Order

1. Finish the dual-target Contract SDK mechanism and prove shared row/action
   values across host and guest.
2. Design neutral shared table/index descriptors and ABI/schema identity.
3. Add the generic read contract, state anchors, bounded cursors and typed host
   views over `forge_api_core`.
4. Add a node plugin implementation and transport-parity tests over HTTP and
   P2P.
5. Add transaction submission/status contracts using existing Chain protocol
   values.
6. Add a Spring compatibility adapter with explicitly weaker capabilities where
   the donor API cannot preserve a pinned view.

Each block requires donor traceability, focused libraries, package consumers
and review of public stability before implementation.

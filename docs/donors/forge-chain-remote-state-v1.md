# Forge Chain Remote State Donor Baseline v1

This note records the donor boundaries behind the planned transport-neutral
Chain state API. It does not describe a shipped Forge library.

## Donors

- Spring `e6a99f68b67abc4d89fe716755b2e1394a4991f7`:
  - `plugins/chain_plugin/include/eosio/chain_plugin/chain_plugin.hpp`;
  - `plugins/chain_plugin/chain_plugin.cpp`;
  - `plugins/chain_api_plugin/chain_api_plugin.cpp`.
- CDT `69599db279b7b93d0688502720c15c6962a1401b`:
  - `libraries/eosiolib/contracts/eosio/multi_index.hpp`.
- FORGE `66fa2a2a723773a1515e801c990b95af0f548e44`:
  - `libraries/api/core`;
  - `libraries/api/transport`;
  - `libraries/db/core`;
  - `libraries/chain/core`;
  - `libraries/chain/protocol`;
  - `guest/libraries/contract`.

Donor repositories are compatibility and architecture oracles, not build
dependencies.

## Observed Boundaries

CDT `multi_index` is a guest execution API over database intrinsics. It binds a
compile-time table and index declaration to calls such as `db_store_i64`,
`db_find_i64` and the secondary-index intrinsic families. It is not a remote
client and does not own transport, retries or finality.

Spring separates read-only table access from transaction submission:

- `read_only::get_table_rows` accepts contract, scope, table, bounds, index,
  encoding, direction and page limit;
- its result contains rows, `more` and `next_key`;
- `read_write::push_transaction` and `send_transaction` are separate endpoints
  with transaction-specific results;
- `chain_api_plugin` binds these operations to HTTP routes.

The inspected `get_table_rows` request and response do not carry a concrete
block anchor. Consecutive pages therefore cannot be treated as a repeatable
snapshot without an additional node/state-history mechanism.

Forge API already owns typed local/remote contracts, method descriptors,
generated proxies, dispatch and channel bindings. Forge DB Core owns stable
backend snapshots and commit/rollback semantics. Forge Chain owns deterministic
protocol records and neutral chain mechanisms.

## Accepted Patterns

- Preserve the donor split between guest table mutation, remote state reads and
  transaction submission.
- Reuse compile-time table/index declarations for type-safe host queries.
- Keep the remote node contract generic instead of registering an API per
  downstream contract.
- Use bounded lower/upper-bound queries and opaque continuation cursors.
- Keep submission, execution status and irreversible finality distinct.
- Implement the service contract once over Forge API and bind it to HTTP, P2P
  and other transports without Chain-to-transport dependencies.
- Provide a Spring compatibility adapter as an explicitly capability-limited
  implementation.

## Rejected Patterns

- Implementing remote chain access as `forge::db::core::driver`.
- Presenting a submitted transaction as a committed DB transaction.
- Direct remote insert/update/erase of contract rows.
- Treating Spring `get_table_rows` pagination as a pinned snapshot.
- Making Spring JSON variants the canonical typed Forge contract.
- Copying transport routing, HTTP codecs or P2P peer policy into the Chain
  library.
- Linking a blockchain node to every downstream C++ contract library in order
  to serve generic state queries.
- Duplicating table/index schema independently in guest and host code.

## Forge Target Direction

The planned Chain layer defines:

- neutral state selectors, anchors, table queries, pages and typed errors;
- a public `FORGE_API` contract exported through `FORGE_EXPORT_API`;
- typed host views driven by shared downstream contract descriptors;
- separate transaction submission and finality-observation contracts.

A node plugin provides the concrete implementation. Client applications obtain
the generated API proxy through a connected Forge API remote mount. Concrete
transport setup remains outside the Chain library.

Exact target, component, module and namespace names remain subject to the
`create-library` design pass when implementation begins.

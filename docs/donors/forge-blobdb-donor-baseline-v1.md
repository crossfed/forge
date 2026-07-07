# Forge BlobDB Donor Baseline + RocksDB Blob Enablement (v1)

This note is the donor baseline and work scope for two related changes:

1. Extend `forge::rocksdb` so column families carry per-family options,
   including RocksDB's native blob-file (KV-separation) settings.
2. Add a new neutral library `forge::db::blob`: a content-addressed blob store,
   sibling to `forge::db::object`, so products stop hand-rolling large-immutable
   payload storage.

Like `forge::db::object`, `forge::db::blob` must remain a **neutral library of
primitives**. It must not become a RocksDB engine, a runtime plugin, a
blockchain database, a FUSE database, or a product policy layer. It must not
own a hash algorithm choice, erasure coding, encryption, chunking, manifest or
namespace semantics -- those belong to products.

`forge::db::blob` is not implemented by this note.

## Current State (starting point)

- `forge::rocksdb::config::column_families` is `std::vector<std::string>` --
  names only. Families cannot be tuned; all open with default options. There is
  **no** blob-file / value-log / KV-separation support anywhere in
  `forge::rocksdb`.
- `forge::db::rocksdb::config` maps shared `forge::db` families onto RocksDB
  column families. objectdb is a typed-object / secondary-index / transaction /
  snapshot database -- the right home for **metadata**, the wrong home for large
  opaque bytes.
- There is no blob primitive. Large immutable content-addressed payloads
  (Storlane erasure pieces / block bytes, blockchain block bodies, artifacts)
  have no shared Forge component, so each product would reinvent value-log
  usage, content-addressing, pinning and GC.

## Database Class

A **content-addressed blob store** owns: large opaque immutable values
addressed by their content digest; put / get / has / erase; verify-on-read;
reference counting (pin / unpin); garbage collection of unreferenced blobs;
stat/size; streaming reads for large values.

It does **not** own:
- object identity, typed records, secondary indexes, transaction/snapshot
  object semantics -- that is `forge::db::object` (blobs are addressed by a
  content digest, **not** by `forge::ids::object_id`);
- ordered key/value mechanics, transactions, column families -- that is
  `forge::rocksdb`;
- which hash function, erasure scheme, encryption, chunk sizing, dedup policy,
  manifest structure -- that is the **product**.

Keep the classes separate, exactly as the `forge::db::object` baseline does.

## Non-Goals / Discipline

- **Do not hand-roll a value-log or blob GC engine.** Lean on RocksDB BlobDB
  (WiscKey-style KV-separation), which already provides blob files, blob GC and
  crash recovery. A from-scratch value-log is the one true wheel here -- do not
  build it. `forge::db::blob` is a **thin** abstraction over proven backends.
- **Mechanism, not policy.** `forge::db::blob` exposes content-addressed
  put/get/pin/gc; the product injects the hash function and owns erasure /
  encryption / chunking / dedup.
- **Minimal first slice**, then grow -- mirror the objectdb incremental
  discipline (its first slice deliberately did not own runtime transactions).
- Follow the `create-library` skill for file layout (public `.cppm` under
  `include/forge/blobdb/`, private `details/*.hxx`, impl `.cpp` at unit root).
- Backend binding libraries mirror objectdb naming:
  `forge::db::blob::rocksdb` (lib `blobdb_rocksdb`), later
  `forge::db::blob::fs` (lib `blobdb_fs`).

---

## Work Item 1 -- `forge::rocksdb`: per-family options incl. blob files

**Problem.** `column_families` is name-only; no per-family tuning; no blob
support. Storing large values in any family today pays full LSM
write-amplification (repeated compaction of immutable payload).

**Change.**
- Introduce a per-family options struct and make `config` carry a list of
  `{ name, options }` instead of bare names. Preserve a backward-compatible
  path (a plain name -> default options).
- Options must at minimum surface RocksDB's native blob settings:
  `enable_blob_files`, `min_blob_size`, `blob_file_size`,
  `blob_compression_type`, `enable_blob_garbage_collection`,
  `blob_garbage_collection_age_cutoff`. Include the general per-CF knobs that
  will be wanted anyway (value compression, block size, block-cache sizing).
- Extend the `forge::schema` rules for `config` to describe the new per-family
  options (config stays schema-driven, as it is now).

**Discipline.** This is **exposing** RocksDB BlobDB, not building one.
`forge::rocksdb` stays the neutral ordered-KV + blob substrate -- it must not
gain content-addressing, pin semantics or GC policy (those live in
`forge::db::blob`).

**Tests.** A blob-configured family round-trips large values; blob GC runs;
default-options families are unchanged; schema parses per-family options.

---

## Work Item 2 -- `forge::db::blob` library

### Backend neutrality

Driver abstraction is now shared as `forge::db::driver`. Planned/current
backends:
- `forge::db::rocksdb` -- over blob-configured `forge::rocksdb` families
  (KV-separation). Good default for small/medium and mixed blobs.
- `forge::db::blob::fs` (later) -- loose content-addressed files on disk
  (Git/IPFS-flat-fs style). Often superior for very large immutable payloads:
  direct sendfile/mmap, OS page cache, trivial P2P serving.
- remote / object-store (later).

The product picks a backend per workload. Backend neutrality is what justifies
the library over a bare rocksdb helper.

### API shape (first slice = minimal)

Keys are opaque content digests (bytes), not `forge::ids`. The hash function is
**injected by the product** -- `forge::db::blob` does not choose SHA-256.

- `put(digest, bytes)` with verify-on-write, or `put(bytes)` using an injected
  hasher -> returns digest. Content-addressed writes are idempotent.
- `open(digest) -> reader` / `get(digest) -> bytes` (streaming for large blobs).
- `has(digest) -> bool`, `stat(digest) -> { size, ... }`.
- `erase(digest)`.
- Optional verify-on-read flag (re-hash and compare).

First slice ships content-addressed put/get/has/erase + verify over **one**
backend. No pin/GC yet (or a trivial refcount stub).

### Second slice

- `pin(digest, owner)` / `unpin(digest, owner)`, reference counting.
- `gc()` -- collect unreferenced (unpinned, zero-refcount) blobs.
- stat/size, range/streaming reads finalized.

Pin/refcount is the mechanism under both durable piece retention and
reserved-cache pinning in downstream products -- keep it a neutral primitive,
let products define what a "pin owner" means.

### Composition with `forge::db::object`

The intended product pattern:
- Metadata lives in `forge::db::object` as typed objects; an object carries a
  **content-digest reference** (`blob_ref`) to its payload.
- The payload bytes live in `forge::db::blob`.
- Both can sit on one `forge::rocksdb` store (object family + blob family), so a
  write can be **cross-family atomic** (write blob, commit the referencing
  object). Alternatively use blob-then-reference ordering: because
  content-addressed writes are idempotent and orphans are GC-collectable, a
  crash between the two is safe.

This keeps "single source of truth + atomicity + snapshots + flexible search"
(objectdb) while large immutable bytes bypass the object model and the metadata
LSM (blobdb).

---

## Donor Systems

### IPFS blockstore / Bitswap

Accepted:
- content-addressed `Put/Get/Has/Delete` by digest;
- `pin` / `unpin` and GC of unpinned blocks as the reference model;
- has-before-get and existence checks as first-class.

Rejected:
- CID multiformats, IPLD, DAG semantics;
- Bitswap, DHT, networking -- that is a product / other-layer concern.

### Git object store

Accepted:
- content-addressed immutable objects;
- **loose vs packed** tiering as a design idea (small objects loose, cold ones
  packed) -- as an optional later optimization, not the first slice;
- reachability/refcount thinking and `gc`/repack discipline.

Use with care:
- delta/pack compression is an optimization, not a baseline requirement.

Rejected:
- Git object types (blob/tree/commit/tag), refs, and Git's specific hash;
  `forge::db::blob` stores opaque bytes only.

### RocksDB BlobDB / WiscKey

Accepted:
- KV-separation (value-log / blob files) as the `rocksdb` backend engine;
- native blob GC and blob settings.

Use with care:
- blob files are best for small/medium values; very large payloads may prefer
  the `fs` backend.

Rejected:
- writing our own value-log or blob GC.

### Ceph RADOS / S3-style object stores

Accepted:
- object = opaque immutable bytes + name/digest;
- `GET/PUT/DELETE/HEAD` shape as the eventual remote-backend contract.

Rejected:
- placement/CRUSH/replication/distribution -- product / network layer.

### Bitcoin block & undo files (`blk*.dat`)

Accepted:
- append-only flat files of immutable records + an external index as a
  dead-simple, robust `fs` backend model for huge sequential blobs.

Rejected:
- chain-specific record framing and validation.

---

## Boundaries Summary

- `forge::rocksdb` -- ordered KV + column families + **blob files (engine)** +
  transactions/snapshots. Neutral substrate. No content-addressing.
- `forge::db::blob` -- **content-addressed** blob mechanism: put/get/has/erase,
  verify, pin/refcount, GC. Backend-neutral. No hash choice, no erasure, no
  encryption, no chunking.
- `forge::db::object` -- typed objects, secondary indexes, transactions,
  snapshots. Holds metadata; references blobs by digest.
- **Product** -- chooses hash function, erasure/encryption/chunking/dedup,
  manifest and namespace semantics, GC and retention policy, pin ownership.

## Deliverables (suggested order)

1. `forge::rocksdb`: per-family options struct + blob settings + schema + tests.
2. Commit this note as `docs/donors/forge-blobdb-donor-baseline-v1.md`.
3. `forge::db::blob` skeleton per `create-library`: `driver`, `store`,
   `digest`/`blob_ref` types, exceptions; first-slice API
   (put/get/has/erase + verify).
4. `forge::db::blob::rocksdb` driver over a blob-configured family + tests.
5. Second slice: pin/refcount + `gc()`; `forge::db::blob::fs` backend.

Keep every layer neutral. No Storlane / blockchain / product policy inside
`forge::rocksdb` or `forge::db::blob`.

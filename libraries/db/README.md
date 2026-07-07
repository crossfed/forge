# forge_db

`forge_db` is the shared low-level record driver layer. It owns the public
record-oriented contract used by higher-level stores such as ObjectDB and
BlobDB.

## Scope

- `forge::db::record_key`, `record_range`, `record_entry`, `record_page`,
  `cursor`, `page_request` and `family`.
- `forge::db::session`: backend-owned async record session.
- `forge::db::driver`: opens write transactions and read snapshots.
- `forge::db::transaction`: move-only commit/rollback boundary with participant
  hooks.
- `forge::db::snapshot`: stable read-only view.

`forge_db` does not know about objects, blobs, RocksDB, plugins, paths, WAL
policy or product schemas.

## Driver Contract

A backend implements `forge::db::driver` by opening sessions with honest
capabilities:

- `snapshot_reads=false, writes=true`: write transaction session.
- `snapshot_reads=true, writes=false`: read-only stable snapshot.
- `snapshot_reads=true, writes=true`: universal session.
- `snapshot_reads=false, writes=false`: invalid and rejected.

`transaction` owns commit/rollback and participant cleanup hooks. Higher-level
libraries can join the same transaction and share one backend commit boundary.

## Families

`forge::db::family` is a logical record space inside one driver. RocksDB maps it
to column families. In-memory drivers can map it to separate ordered maps.

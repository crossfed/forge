# Forge DB Shared Read Snapshot v1

## Donor Baseline

The implementation follows RocksDB tag `v11.1.1` at commit
`6cdeb9d9d0630763327f512e6255cab33f6834e7`. The inspected donor surfaces are
`include/rocksdb/db.h`
(`GetSnapshot`/`ReleaseSnapshot`) and `include/rocksdb/options.h`
(`ReadOptions::snapshot`):

- `rocksdb::DB::GetSnapshot()` creates one point-in-time view for all column
  families in the database;
- `rocksdb::ReadOptions::snapshot` applies that view to reads and iterators;
- `rocksdb::DB::ReleaseSnapshot()` releases retained versions only after the
  last owner is done.

Forge already wrapped this lifecycle in `forge_rocksdb`. The DB adapter maps one
native snapshot session to `forge::db::core::snapshot`; DB Object and DB Blob do
not open additional native snapshots when joining it.

## Accepted Patterns

- one native snapshot shared by all logical read layers;
- RAII ownership retained through copyable read handles;
- point-in-time visibility for records from every configured family;
- explicit warning that long-lived snapshots retain old versions and files;
- concurrent read-only use of snapshot copies.

## Rejected Patterns

- independently opening Object and Blob snapshots and assuming they represent
  the same point;
- exposing a mutable Blob transaction as a read view;
- exporting the raw Core snapshot through the DB Store plugin wrapper;
- invalidating an already opened read operation during plugin shutdown;
- daemon-lifetime snapshots that hold compaction history indefinitely.

## Forge Mapping

- DB Core owns stable driver-origin validation and native snapshot lifetime.
- DB Object joins the Core snapshot through its existing decoder and indexes.
- DB Blob provides a read-only snapshot over data and reference families.
- DB Store opens one Core snapshot and eagerly constructs configured views.
- RocksDB integration tests delete metadata and Blob ownership in a concurrent
  transaction, collect the payload, and prove that the old unified snapshot can
  still read metadata, index entries, owner count and payload bytes.

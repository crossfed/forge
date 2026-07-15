# forge_db_mdbx

`forge_db_mdbx` implements `forge::db::core::driver` over the vendored
libmdbx `v0.14.2` release. It is intended for ordered state stores with one
serialized writer, many concurrent snapshot readers and strict transactional
savepoint requirements.

## Package

- Target: `forge_db_mdbx`
- Package component: `db_mdbx`
- Modules: `forge.db.mdbx.driver`, `forge.db.mdbx.exceptions`
- Dependencies: `forge_asio`, `forge_db_core`; libmdbx is private and vendored
- Build option: `FORGE_ENABLE_MDBX=ON`

## Usage

```cpp
import forge.asio.affine;
import forge.db.mdbx.driver;

forge::asio::affine::lane lane{
   {.max_pending_operations = 1024,
    .max_waiting_submissions = 1024,
    .thread_name = "db-mdbx"}
};

auto driver = co_await forge::db::mdbx::driver::open(
   {
      .path = "./data/mdbx",
      .families = {"objectdb", "blobdb.data", "blobdb.refs"},
      .durability_mode = forge::db::mdbx::durability::durable_sync,
      .map = {.upper_size = 64ULL * 1024 * 1024 * 1024,
              .growth_step = 256ULL * 1024 * 1024},
   },
   lane.get_executor());

auto transaction = co_await driver->begin_transaction();
co_await transaction.put({"objectdb"}, key, value);
co_await transaction.commit();

co_await driver->async_close();
driver.reset();
co_await lane.shutdown();
```

The lane owner must outlive every driver and session. Call `async_close()` and
release active transactions/snapshots before shutting the lane down.

## Execution Contract

- Environment open, write begin, CRUD, native savepoints, commit, rollback,
  flush and close run on the supplied affine executor.
- A FIFO `forge::asio::gate` admits one writer without blocking an ordinary
  Asio runtime worker on the MDBX writer mutex.
- Snapshot reads run through independent clones of one immutable anchor
  transaction. Clone creation is briefly serialized because libmdbx forbids
  simultaneous use of the anchor; reads and cursors themselves remain
  concurrent.
- Keys and values are copied at the Core boundary. Native handles and return
  codes are never exported.

## Durability

`durable_sync` is the default and acknowledges a commit only after MDBX's safe
durable commit path. `safe_nosync` keeps a valid older steady checkpoint but may
lose the unsteady committed tail after an operating-system crash. Use it only
when the application has a tested replay source.

`async_flush(true)` creates a forced steady checkpoint. `async_flush(false)`
performs a non-blocking sync poll. Both are serialized with the writer gate.

## Geometry And Snapshots

Production deployments should configure an explicit upper map size and a
realistic growth step. Reaching the configured map bound, exhausting readers,
incompatible environment geometry and I/O failures are reported as typed
`forge::db::mdbx::exceptions`.

Long-lived snapshots retain old MVCC pages and can grow the environment. Keep
snapshots operation-scoped or bounded to a known batch; they are not a
daemon-lifetime cache.

## Boundaries

This library does not own Object, Blob, Revision, ranked-index or plugin
policy. Those layers consume the neutral DB Core driver. MDBX configuration in
`plugins.db.store` is a separate integration; this library can already be
passed through the plugin's programmatic `add_store()` API.

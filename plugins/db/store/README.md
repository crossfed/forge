# DB Store Plugin

`forge::plugins::db::store` owns configured named physical DB stores for
applications. Each named store owns one `forge::db::core::driver` and may expose
`forge::db::object::store`, `forge::db::blob::store`, or both as logical layers.

- Target: `forge_plugins_db_store`
- Package component: `plugins_db_store`
- Runtime id and API id: `forge.plugins.db.store`
- Config section: `plugins.db.store`

The plugin handles physical store setup, lifecycle, status and flushing. It does
not describe C++ object schemas or blob retention policy in YAML. Domain code
still declares object/index descriptors in C++ and registers them on
`store_handle.objects()`.

## Config

```yaml
plugins:
  db:
    store:
      stores:
        - name: "witness"
          driver: "rocksdb"
          path: "./data/rocksdb/witness"
          object:
            family: "objectdb"
            write-policy: "single-writer"
          blob:
            data-family: "blobdb.data"
            refs-family: "blobdb.refs"
            data-blobs:
              enable-blob-files: true
              min-blob-size: 4096
```

`driver: rocksdb` is available when Forge is built with RocksDB support. Custom
drivers are added programmatically through the local API during plugin
`initialize()`, before the DB Store `after_initialize()` phase opens every
configured layer.

## Usage

```cpp
auto db = context.apis().get<forge::plugins::db::store::api>(
   forge::plugins::db::store::api::ref());

auto witness = co_await db->store("witness");
witness.objects().register_object<witness_object>();

auto tx = co_await witness.begin_transaction();
auto objects = co_await witness.objects().join(tx);
auto blobs = witness.blobs().join(tx);

auto content = co_await blobs.put(bytes);
co_await objects.insert(witness_record{.content = content});
co_await tx.commit();
```

When the named store has an Object layer, `begin_transaction()` reserves that
layer's writer lane. `objects().join(tx)` reuses the already attached Object
participant, while `blobs().join(tx)` attaches Blob state to the same Core
transaction. A transaction created by another named store is rejected.

Use `add_store(name, driver, options)` during setup when an application provides
its own `forge::db::core::driver`. Once every plugin has initialized, DB Store
opens its drivers and logical layers and enters the private `ready` phase.
Application `after_initialize` callbacks can then obtain handles and register
object models before startup:

```cpp
builder.after_initialize([](const forge::app::application_context& context)
                            -> boost::asio::awaitable<void> {
   auto db = context.api_view().get<forge::plugins::db::store::api>(
      forge::plugins::db::store::api::ref());
   auto witness = co_await db->store("witness");
   witness.objects().register_object<witness_object>();
});
```

`status().stores[i].started` remains false in `ready` and becomes true only
after plugin startup. In `ready`, `objects()` permits only object registration,
interceptor registration and observer registration. Transactions, reads,
writes, indexes, Blob access and flushes remain unavailable until startup. New
stores are rejected from `ready` onward, while opened handles remain available
until shutdown closes the physical store.

# DB Object Plugin

`forge::plugins::db::object` owns configured named `forge::db::object::store`
instances for applications.

- Target: `forge_plugins_db_object`
- Package component: `plugins_db_object`
- Runtime id and API id: `forge.plugins.db.object`
- Config section: `plugins.db.object`

The plugin handles physical store setup, lifecycle, status and flushing. It does
not describe C++ object schemas in YAML. Domain code still declares object and
index descriptors with `forge.db.object.object` / `forge.db.object.index`, then
registers them on a `store_handle`.

## Config

```yaml
plugins:
  db:
    object:
      stores:
        - name: "witness"
          driver: "rocksdb"
          path: "./data/rocksdb/witness"
          family: "objectdb"
          write-policy: "single-writer"
```

`driver: rocksdb` is available when Forge is built with RocksDB support. Custom
drivers are added programmatically through the local API before plugin startup.

## Usage

```cpp
auto db = context.apis().get<forge::plugins::db::object::api>(
   forge::plugins::db::object::api::ref());

auto witness = co_await db->store("witness");
witness.register_object<witness_object>();

co_await witness.insert(witness_record{...});
auto loaded = co_await witness.get(witness_id{42});
```

Use `add_store(name, driver, options)` during setup when an application provides
its own `forge::db::core::driver`. After startup, new stores are rejected so
handles stay tied to the plugin lifecycle.

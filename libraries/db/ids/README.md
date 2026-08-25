# forge_db_ids

`forge_db_ids` is the DB-family leaf library for compact database object
identifiers. It provides stable object identity without depending on a driver,
transaction, Object store or application runtime.

Use it when a type needs a compact, typed `space/type/instance` identity:

```cpp
import forge.db.ids.typed_id;

using account_id = forge::db::ids::typed_id<1, 2>;

auto account = account_id{42};
```

## Public Modules

- `forge.db.ids.typed_id` defines guest-safe `typed_id<Space, Type>`, typed-id traits, the `type_for_id` extension point and instance-only Raw serialization.
- `forge.db.ids.object_id` re-exports `forge.db.ids.typed_id` and adds `object_id`, object-id conversion and matching, `try_typed`, string formatting, and host Variant conversion.

The CMake target is `forge_db_ids`; the installed Forge component is `db_ids`.

## Identity Model

`object_id` has three fields:

- `space`: high-level object namespace.
- `type`: object kind inside the space.
- `instance`: object instance number.

`typed_id<Space, Type>` is the strongly typed form for APIs that know the exact object kind at compile time. Host code can import `forge.db.ids.object_id`, convert it with `to_object_id(...)`, and use `try_typed<Space, Type>(...)` when decoding generic IDs.

## Serialization

Raw serialization writes fields in this exact order:

```text
space, type, instance
```

Variant conversion for `object_id` uses an object with `space`, `type`, and `instance`. The host `forge.db.ids.object_id` module also provides instance-only Variant conversion for `typed_id<Space, Type>`.

## Boundaries

This library does not define tables, indexes, sessions, transactions or backend
storage. DB Object, Blob and Revision build on its value types. DB Object owns
its separate `index_for_id` mapping; `type_for_id` does not imply an index or
persisted model registration.

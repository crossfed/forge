# forge_db_ids

`forge_db_ids` is the DB-family leaf library for compact database object
identifiers. It provides stable object identity without depending on a driver,
transaction, Object store or application runtime.

Use it when a type needs a compact, typed `space/type/instance` identity:

```cpp
import forge.db.ids.object_id;

using account_id = forge::db::ids::typed_id<1, 2>;

auto account = account_id{42};
auto generic = account.as_object_id(); // {space=1, type=2, instance=42}
```

## Public Modules

- `forge.db.ids.object_id` defines `forge::db::ids::object_id`, `typed_id<Space, Type>`, typed-id traits, a generic `type_for_id` extension point, conversion helpers, string formatting, raw serialization, and variant conversion.

The CMake target is `forge_db_ids`; the installed Forge component is `db_ids`.

## Identity Model

`object_id` has three fields:

- `space`: high-level object namespace.
- `type`: object kind inside the space.
- `instance`: object instance number.

`typed_id<Space, Type>` is the strongly typed form for APIs that know the exact object kind at compile time. Convert to a generic `object_id` with `as_object_id()`, and use `try_typed<Space, Type>(...)` when decoding generic IDs.

## Serialization

Raw serialization writes fields in this exact order:

```text
space, type, instance
```

Variant conversion for `object_id` uses an object with `space`, `type`, and `instance`. Variant conversion for `typed_id<Space, Type>` keeps the compact instance-only representation.

## Boundaries

This library does not define tables, indexes, sessions, transactions or backend
storage. DB Object, Blob and Revision build on its value types. DB Object owns
its separate `index_for_id` mapping; `type_for_id` does not imply an index or
persisted model registration.

module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.db.object.transaction;

import forge.ids.object_id;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.object.cursor;
import forge.db.object.exceptions;
import forge.db.object.hooks;
import forge.db.object.index;
import forge.db.object.object;
import forge.raw.raw;

export namespace forge::db::object {

class transaction {
 public:
   using ensure_registered_fn = std::function<void(forge::ids::object_id, std::type_index)>;
   using allocate_id_fn = std::function<boost::asio::awaitable<forge::ids::object_id>(forge::ids::object_id,
                                                                                      forge::db::core::transaction&)>;
   using allocation_seal_map = std::map<forge::ids::object_id, std::uint64_t>;
   using seal_allocations_fn = std::function<boost::asio::awaitable<void>(allocation_seal_map)>;
   using release_fn = std::function<void()>;

   transaction() = default;
   transaction(forge::db::core::transaction&& active, forge::db::core::family family, ensure_registered_fn ensure,
               allocate_id_fn allocate, std::vector<std::shared_ptr<interceptor>> interceptors,
               std::vector<std::shared_ptr<observer>> observers, release_fn release);
   transaction(forge::db::core::transaction&& active, forge::db::core::family family, ensure_registered_fn ensure,
               allocate_id_fn allocate, seal_allocations_fn seal,
               std::vector<std::shared_ptr<interceptor>> interceptors, std::vector<std::shared_ptr<observer>> observers,
               release_fn release, boost::asio::any_io_executor cleanup_executor);
   transaction(forge::db::core::transaction& active, forge::db::core::family family, ensure_registered_fn ensure,
               allocate_id_fn allocate, seal_allocations_fn seal,
               std::vector<std::shared_ptr<interceptor>> interceptors,
               std::vector<std::shared_ptr<observer>> observers);
   transaction(forge::db::core::transaction& active, forge::db::core::family family, ensure_registered_fn ensure,
               allocate_id_fn allocate, std::vector<std::shared_ptr<interceptor>> interceptors,
               std::vector<std::shared_ptr<observer>> observers);

   transaction(forge::db::core::transaction&& active, forge::db::core::family family, ensure_registered_fn ensure,
               std::vector<std::shared_ptr<interceptor>> interceptors, std::vector<std::shared_ptr<observer>> observers,
               release_fn release);
   transaction(forge::db::core::transaction&& active, forge::db::core::family family, ensure_registered_fn ensure,
               std::vector<std::shared_ptr<interceptor>> interceptors, std::vector<std::shared_ptr<observer>> observers,
               release_fn release, boost::asio::any_io_executor cleanup_executor);
   transaction(forge::db::core::transaction& active, forge::db::core::family family, ensure_registered_fn ensure,
               std::vector<std::shared_ptr<interceptor>> interceptors,
               std::vector<std::shared_ptr<observer>> observers);

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> get(Id id);

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> find(Id id);

   template <object_model Object> boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id);

   template <object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id);

   template <application_object_value Value> boost::asio::awaitable<void> insert(Value value);

   template <application_object_value Value, typename Fn>
      requires std::default_initializable<Value> && std::invocable<Fn&, Value&>
   boost::asio::awaitable<Value> create(Fn&& fn);

   template <application_object_value Value> boost::asio::awaitable<void> replace(Value value);

   template <forge::ids::typed_id_like Id, typename Fn>
      requires application_object_model<object_index_for_id_t<Id>>
   boost::asio::awaitable<void> modify(Id id, Fn&& fn);

   template <forge::ids::typed_id_like Id>
      requires application_object_model<object_index_for_id_t<Id>>
   boost::asio::awaitable<void> erase(Id id);

   template <application_object_model Object> boost::asio::awaitable<void> erase(forge::ids::object_id id);

   template <object_model Object, typename Tag> [[nodiscard]] index_view<Object, Tag> index() const;

   [[nodiscard]] forge::db::core::transaction& db_transaction() const;

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   class access;

   [[nodiscard]] change_set& changes() const;
   [[nodiscard]] forge::db::core::transaction& active_transaction() const;

   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;
   boost::asio::awaitable<void> before_mutation(const object_mutation& mutation) const;
   boost::asio::awaitable<forge::ids::object_id> allocate_id(forge::ids::object_id type) const;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get_record(forge::db::core::record_key key) const;
   boost::asio::awaitable<void> put_record(forge::db::core::record_key key, std::vector<std::byte> value) const;
   boost::asio::awaitable<void> erase_record(forge::db::core::record_key key) const;
   boost::asio::awaitable<forge::db::core::record_page> scan_records(forge::db::core::record_range range,
                                                                     forge::db::core::page_request request) const;

   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::db::object

namespace forge::db::object {

class transaction::access {
 public:
   explicit access(const transaction& owner) : owner_{owner} {}

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::core::record_key key) const {
      co_return co_await owner_.get_record(std::move(key));
   }

   boost::asio::awaitable<void> put(forge::db::core::record_key key, std::vector<std::byte> value) const {
      co_await owner_.put_record(std::move(key), std::move(value));
   }

   boost::asio::awaitable<void> erase(forge::db::core::record_key key) const {
      co_await owner_.erase_record(std::move(key));
   }

   boost::asio::awaitable<forge::db::core::record_page> scan_page(forge::db::core::record_range range,
                                                                  forge::db::core::page_request request) const {
      co_return co_await owner_.scan_records(std::move(range), std::move(request));
   }

   [[nodiscard]] change_set& changes() const {
      return owner_.changes();
   }

   template <object_model Object> void ensure_registered() const {
      owner_.ensure_registered_type(object_id_of<Object>::value, std::type_index{typeid(Object)});
   }

   boost::asio::awaitable<void> before_mutation(const object_mutation& mutation) const {
      return owner_.before_mutation(mutation);
   }

   boost::asio::awaitable<forge::ids::object_id> allocate_id(forge::ids::object_id type) const {
      co_return co_await owner_.allocate_id(type);
   }

 private:
   const transaction& owner_;
};

} // namespace forge::db::object

#include "ordered_key.hxx"

namespace forge::db::object::detail {

template <typename T> std::vector<std::byte> to_byte_vector(const std::vector<T>& input) {
   auto out = std::vector<std::byte>{};
   out.reserve(input.size());
   for (auto value : input) {
      out.push_back(static_cast<std::byte>(value));
   }
   return out;
}

inline std::vector<std::uint8_t> to_uint8_vector(const std::vector<std::byte>& input) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(input.size());
   for (auto value : input) {
      out.push_back(static_cast<std::uint8_t>(value));
   }
   return out;
}

template <typename T> std::vector<std::byte> pack_value(const T& value) {
   auto bytes = std::vector<std::uint8_t>{};
   forge::raw::pack(bytes, value);
   return to_byte_vector(bytes);
}

template <typename T> T unpack_value(const std::vector<std::byte>& bytes) {
   return forge::raw::unpack<T>(to_uint8_vector(bytes));
}

template <object_model Object> id_t_of<Object> typed_id_from(forge::ids::object_id id) {
   if (!forge::ids::matches<id_t_of<Object>::space, id_t_of<Object>::type>(id)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "object_id does not match db object type");
   }
   return id_t_of<Object>{id};
}

template <object_model Object, typename Access>
boost::asio::awaitable<std::optional<typename Object::value_type>> read_transaction_object(Access tx,
                                                                                           forge::ids::object_id id) {
   tx.template ensure_registered<Object>();
   const auto typed = typed_id_from<Object>(id);
   const auto key = detail::ordered_key::object_record_key<Object>(typed);
   const auto bytes = co_await tx.get(key);
   if (!bytes.has_value()) {
      co_return std::nullopt;
   }
   co_return unpack_value<typename Object::value_type>(*bytes);
}

template <object_model Object, typename Access>
boost::asio::awaitable<object_page<typename Object::value_type>>
page_transaction_objects(Access tx, forge::db::core::record_range range, forge::db::core::page_request request) {
   tx.template ensure_registered<Object>();
   forge::db::object::validate_page_request(request);

   auto records = co_await tx.scan_page(std::move(range), std::move(request));
   auto out = object_page<typename Object::value_type>{};
   out.next = std::move(records.next);

   for (const auto& entry : records.entries) {
      const auto id = unpack_value<id_t_of<Object>>(entry.value);
      auto value = co_await read_transaction_object<Object>(tx, id.as_object_id());
      if (!value.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::not_found, "db object index points to a missing object");
      }
      out.items.push_back(std::move(*value));
   }

   co_return out;
}

struct materialized_index {
   forge::db::core::record_key key;
   bool unique = false;
};

template <object_model Object, std::size_t Index = 0>
void materialize_indexes(const typename Object::value_type& value, std::vector<materialized_index>& out) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         out.push_back(materialized_index{
             .key = detail::ordered_key::index_entry_key<Object, typename index::tag_type>(value),
             .unique = index::kind == index_kind::ordered_unique,
         });
      }
      materialize_indexes<Object, Index + 1>(value, out);
   }
}

template <object_model Object>
[[nodiscard]] std::vector<materialized_index> materialize_indexes(const typename Object::value_type& value) {
   auto out = std::vector<materialized_index>{};
   out.reserve(Object::indexes_type::size - 1U);
   materialize_indexes<Object>(value, out);
   return out;
}

template <object_model Object, typename Access>
boost::asio::awaitable<void> verify_unique_indexes(Access tx, const typename Object::value_type& value,
                                                   const std::vector<materialized_index>& indexes) {
   for (const auto& index : indexes) {
      if (!index.unique) {
         continue;
      }
      const auto existing = co_await tx.get(index.key);
      if (existing.has_value()) {
         const auto existing_id = unpack_value<id_t_of<Object>>(*existing);
         if (existing_id != value.id) {
            FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "db object unique index value already exists");
         }
      }
   }
   co_return;
}

template <typename Access, typename Id>
boost::asio::awaitable<void> write_secondary_indexes(Access tx, const std::vector<materialized_index>& indexes, Id id) {
   const auto packed_id = pack_value(id);
   for (const auto& index : indexes) {
      co_await tx.put(index.key, packed_id);
   }
   co_return;
}

template <typename Access>
boost::asio::awaitable<void> remove_secondary_indexes(Access tx, const std::vector<materialized_index>& indexes) {
   for (const auto& index : indexes) {
      co_await tx.erase(index.key);
   }
   co_return;
}

template <typename Access, application_object_value Value>
boost::asio::awaitable<void> insert_object(Access tx, Value value) {
   using object_model_type = object_index_for_id_t<typename Value::id_t>;
   tx.template ensure_registered<object_model_type>();

   const auto object_key = detail::ordered_key::object_record_key<object_model_type>(value.id);
   if ((co_await tx.get(object_key)).has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "db object id already exists");
   }

   auto after = pack_value(value);
   const auto indexes = materialize_indexes<object_model_type>(value);
   auto mutation = object_mutation{
       .kind = mutation_kind::insert,
       .id = value.id.as_object_id(),
       .after = after,
   };
   co_await tx.before_mutation(mutation);
   co_await verify_unique_indexes<object_model_type>(tx, value, indexes);
   co_await tx.put(object_key, std::move(after));
   co_await write_secondary_indexes(tx, indexes, value.id);
   tx.changes().mutations.push_back(std::move(mutation));
   co_return;
}

template <typename Access, application_object_value Value>
boost::asio::awaitable<void> replace_object(Access tx, Value value, mutation_kind kind) {
   using object_model_type = object_index_for_id_t<typename Value::id_t>;
   tx.template ensure_registered<object_model_type>();

   const auto existing = co_await read_transaction_object<object_model_type>(tx, value.id.as_object_id());
   if (!existing.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "db object was not found");
   }

   auto before = pack_value(*existing);
   auto after = pack_value(value);
   const auto old_indexes = materialize_indexes<object_model_type>(*existing);
   const auto new_indexes = materialize_indexes<object_model_type>(value);
   auto mutation = object_mutation{
       .kind = kind,
       .id = value.id.as_object_id(),
       .before = before,
       .after = after,
   };
   co_await tx.before_mutation(mutation);
   co_await verify_unique_indexes<object_model_type>(tx, value, new_indexes);
   co_await remove_secondary_indexes(tx, old_indexes);
   co_await tx.put(detail::ordered_key::object_record_key<object_model_type>(value.id), std::move(after));
   co_await write_secondary_indexes(tx, new_indexes, value.id);
   tx.changes().mutations.push_back(std::move(mutation));
   co_return;
}

template <application_object_model Object, typename Access>
boost::asio::awaitable<void> erase_object(Access tx, forge::ids::object_id id) {
   tx.template ensure_registered<Object>();
   const auto typed = typed_id_from<Object>(id);
   const auto existing = co_await read_transaction_object<Object>(tx, typed.as_object_id());
   if (!existing.has_value()) {
      co_return;
   }

   auto before = pack_value(*existing);
   const auto indexes = materialize_indexes<Object>(*existing);
   auto mutation = object_mutation{
       .kind = mutation_kind::erase,
       .id = typed.as_object_id(),
       .before = before,
   };
   co_await tx.before_mutation(mutation);
   co_await remove_secondary_indexes(tx, indexes);
   co_await tx.erase(detail::ordered_key::object_record_key<Object>(typed));
   tx.changes().mutations.push_back(std::move(mutation));
   co_return;
}

template <typename Access, application_object_value Value, typename Fn>
   requires std::default_initializable<Value> && std::invocable<Fn&, Value&>
boost::asio::awaitable<Value> create_object(Access tx, Fn&& fn) {
   using object_model_type = object_index_for_id_t<typename Value::id_t>;
   tx.template ensure_registered<object_model_type>();

   auto allocated = co_await tx.allocate_id(object_id_of<object_model_type>::value);
   auto value = Value{};
   value.id = typename Value::id_t{allocated};
   const auto generated_id = value.id;

   using result_type = std::invoke_result_t<Fn&, Value&>;
   static_assert(std::is_void_v<result_type>, "db object create initializer must return void");
   std::invoke(fn, value);
   if (value.id != generated_id) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor,
                            "db object create initializer must not change generated id");
   }

   co_await insert_object(tx, value);
   co_return value;
}

} // namespace forge::db::object::detail

export namespace forge::db::object {

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> transaction::get(Id id) {
   co_return co_await get<object_index_for_id_t<Id>>(id.as_object_id());
}

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> transaction::find(Id id) {
   co_return co_await find<object_index_for_id_t<Id>>(id.as_object_id());
}

template <object_model Object>
boost::asio::awaitable<typename Object::value_type> transaction::get(forge::ids::object_id id) {
   const auto value = co_await find<Object>(id);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "db object was not found");
   }
   co_return *value;
}

template <object_model Object>
boost::asio::awaitable<std::optional<typename Object::value_type>> transaction::find(forge::ids::object_id id) {
   co_return co_await detail::read_transaction_object<Object>(access{*this}, id);
}

template <application_object_value Value> boost::asio::awaitable<void> transaction::insert(Value value) {
   co_await detail::insert_object(access{*this}, std::move(value));
   co_return;
}

template <application_object_value Value, typename Fn>
   requires std::default_initializable<Value> && std::invocable<Fn&, Value&>
boost::asio::awaitable<Value> transaction::create(Fn&& fn) {
   co_return co_await detail::create_object<access, Value>(access{*this}, std::forward<Fn>(fn));
}

template <application_object_value Value> boost::asio::awaitable<void> transaction::replace(Value value) {
   co_await detail::replace_object(access{*this}, std::move(value), mutation_kind::replace);
   co_return;
}

template <forge::ids::typed_id_like Id, typename Fn>
   requires application_object_model<object_index_for_id_t<Id>>
boost::asio::awaitable<void> transaction::modify(Id id, Fn&& fn) {
   using object_model_type = object_index_for_id_t<Id>;
   auto next = co_await get(id);
   using result_type = std::invoke_result_t<Fn&, typename object_model_type::value_type&>;
   static_assert(std::is_void_v<result_type>, "db object modify mutator must return void");
   std::invoke(fn, next);
   co_await detail::replace_object(access{*this}, std::move(next), mutation_kind::modify);
   co_return;
}

template <forge::ids::typed_id_like Id>
   requires application_object_model<object_index_for_id_t<Id>>
boost::asio::awaitable<void> transaction::erase(Id id) {
   co_await erase<object_index_for_id_t<Id>>(id.as_object_id());
   co_return;
}

template <application_object_model Object>
boost::asio::awaitable<void> transaction::erase(forge::ids::object_id id) {
   co_await detail::erase_object<Object>(access{*this}, id);
   co_return;
}

template <object_model Object, typename Tag> [[nodiscard]] index_view<Object, Tag> transaction::index() const {
   access{*this}.template ensure_registered<Object>();
   return index_view<Object, Tag>{[owner = *this](forge::db::core::record_range range,
                                                  forge::db::core::page_request request) mutable
                                      -> boost::asio::awaitable<object_page<typename Object::value_type>> {
      co_return co_await detail::page_transaction_objects<Object>(access{owner}, std::move(range), std::move(request));
   }};
}

} // namespace forge::db::object

module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

export module forge.db.object.snapshot;

import forge.ids.object_id;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.object.cursor;
import forge.db.object.exceptions;
import forge.db.object.index;
import forge.db.object.object;
import forge.raw.raw;

export namespace forge::db::object {

class snapshot {
 public:
   using ensure_registered_fn = std::function<void(forge::ids::object_id, std::type_index)>;

   snapshot() = default;
   snapshot(forge::db::core::snapshot active, forge::db::core::family family, ensure_registered_fn ensure);

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<typename index_for_id_t<Id>::value_type> get(Id id);

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<std::optional<typename index_for_id_t<Id>::value_type>> find(Id id);

   template <object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id);

   template <object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id);

   template <object_model Object, typename Tag>
   [[nodiscard]] index_view<Object, Tag> index() const;

 private:
   class access;

   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get_record(forge::db::core::record_key key) const;
   boost::asio::awaitable<forge::db::core::record_page> scan_records(forge::db::core::record_range range, forge::db::core::page_request request) const;

   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::db::object

namespace forge::db::object {

class snapshot::access {
 public:
   explicit access(const snapshot& owner) : owner_{owner} {}

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::core::record_key key) const {
      co_return co_await owner_.get_record(std::move(key));
   }

   boost::asio::awaitable<forge::db::core::record_page> scan_page(forge::db::core::record_range range, forge::db::core::page_request request) const {
      co_return co_await owner_.scan_records(std::move(range), std::move(request));
   }

   template <object_model Object>
   void ensure_registered() const {
      owner_.ensure_registered_type(object_id_of<Object>::value, std::type_index{typeid(Object)});
   }

 private:
   const snapshot& owner_;
};

} // namespace forge::db::object

#include "record_key.hxx"

namespace forge::db::object::detail {

template <object_model Object>
[[nodiscard]] forge::db::core::record_key object_record_key(id_t_of<Object> id) {
   return forge::db::object::detail::record_key::object(id.as_object_id());
}

inline std::vector<std::uint8_t> to_uint8_vector(const std::vector<std::byte>& input) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(input.size());
   for (auto value : input) {
      out.push_back(static_cast<std::uint8_t>(value));
   }
   return out;
}

template <typename T>
T unpack_value(const std::vector<std::byte>& bytes) {
   return forge::raw::unpack<T>(to_uint8_vector(bytes));
}

template <object_model Object>
id_t_of<Object> typed_id_from(forge::ids::object_id id) {
   if (!forge::ids::matches<id_t_of<Object>::space, id_t_of<Object>::type>(id)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "object_id does not match db object type");
   }
   return id_t_of<Object>{id};
}

template <object_model Object, typename Access>
boost::asio::awaitable<std::optional<typename Object::value_type>> read_snapshot_object(Access view,
                                                                                        forge::ids::object_id id) {
   view.template ensure_registered<Object>();
   const auto typed = typed_id_from<Object>(id);
   const auto key = object_record_key<Object>(typed);
   const auto bytes = co_await view.get(key);
   if (!bytes.has_value()) {
      co_return std::nullopt;
   }
   co_return unpack_value<typename Object::value_type>(*bytes);
}

template <object_model Object, typename Access>
boost::asio::awaitable<object_page<typename Object::value_type>> page_snapshot_objects(Access view,
                                                                                       forge::db::core::record_range range,
                                                                                       forge::db::core::page_request request) {
   view.template ensure_registered<Object>();
   forge::db::object::validate_page_request(request);

   auto records = co_await view.scan_page(std::move(range), std::move(request));
   auto out = object_page<typename Object::value_type>{};
   out.next = std::move(records.next);

   for (const auto& entry : records.entries) {
      const auto id = unpack_value<id_t_of<Object>>(entry.value);
      auto value = co_await read_snapshot_object<Object>(view, id.as_object_id());
      if (!value.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::not_found, "db object index points to a missing object");
      }
      out.items.push_back(std::move(*value));
   }

   co_return out;
}

} // namespace forge::db::object::detail

export namespace forge::db::object {

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<typename index_for_id_t<Id>::value_type> snapshot::get(Id id) {
   co_return co_await get<index_for_id_t<Id>>(id.as_object_id());
}

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<std::optional<typename index_for_id_t<Id>::value_type>> snapshot::find(Id id) {
   co_return co_await find<index_for_id_t<Id>>(id.as_object_id());
}

template <object_model Object>
boost::asio::awaitable<typename Object::value_type> snapshot::get(forge::ids::object_id id) {
   const auto value = co_await find<Object>(id);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "db object was not found");
   }
   co_return *value;
}

template <object_model Object>
boost::asio::awaitable<std::optional<typename Object::value_type>> snapshot::find(forge::ids::object_id id) {
   co_return co_await detail::read_snapshot_object<Object>(access{*this}, id);
}

template <object_model Object, typename Tag>
[[nodiscard]] index_view<Object, Tag> snapshot::index() const {
   access{*this}.template ensure_registered<Object>();
   return index_view<Object, Tag>{
      [owner = *this](forge::db::core::record_range range, forge::db::core::page_request request) mutable
         -> boost::asio::awaitable<object_page<typename Object::value_type>> {
         co_return co_await detail::page_snapshot_objects<Object>(access{owner}, std::move(range), std::move(request));
      },
      [owner = *this]() mutable -> index_page_query<typename Object::value_type> {
         return [owner](forge::db::core::record_range range, forge::db::core::page_request request) mutable
                   -> boost::asio::awaitable<object_page<typename Object::value_type>> {
            co_return co_await detail::page_snapshot_objects<Object>(
               access{owner},
               std::move(range),
               std::move(request));
         };
      }};
}

} // namespace forge::db::object

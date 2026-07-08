module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

export module forge.plugins.db.objectdb.api;

export import forge.plugins.db.objectdb.exceptions;
export import forge.plugins.db.objectdb.types;

import forge.api.core.binding;
import forge.db.driver;
import forge.ids.object_id;
import forge.objectdb.cursor;
import forge.objectdb.hooks;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.snapshot;
import forge.objectdb.store;
import forge.objectdb.transaction;

export namespace forge::plugins::db::objectdb {

class store_handle_state {
 public:
   virtual ~store_handle_state() = default;

   [[nodiscard]] virtual std::string name() const = 0;
   [[nodiscard]] virtual std::shared_ptr<forge::objectdb::store> require_store() const = 0;
   [[nodiscard]] virtual std::shared_ptr<forge::db::driver> require_driver() const = 0;
};

class store_handle {
 public:
   store_handle() = default;
   explicit store_handle(std::shared_ptr<store_handle_state> state) : state_{std::move(state)} {}

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(state_);
   }

   [[nodiscard]] std::string name() const;

   template <forge::objectdb::object_model Object>
   void register_object() const {
      require_store()->template register_object<Object>();
   }

   void add_interceptor(std::shared_ptr<forge::objectdb::interceptor> value) const;
   void add_observer(std::shared_ptr<forge::objectdb::observer> value) const;

   boost::asio::awaitable<forge::objectdb::transaction> begin_transaction() const;
   boost::asio::awaitable<forge::objectdb::snapshot> begin_read() const;

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<typename forge::objectdb::object_index_for_id_t<Id>::value_type> get(Id id) const {
      co_return co_await require_store()->get(id);
   }

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<std::optional<typename forge::objectdb::object_index_for_id_t<Id>::value_type>>
   find(Id id) const {
      co_return co_await require_store()->find(id);
   }

   template <forge::objectdb::object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id) const {
      co_return co_await require_store()->template get<Object>(id);
   }

   template <forge::objectdb::object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id) const {
      co_return co_await require_store()->template find<Object>(id);
   }

   template <forge::objectdb::object_value Value>
   boost::asio::awaitable<void> insert(Value value) const {
      co_await require_store()->insert(std::move(value));
   }

   template <forge::objectdb::object_value Value>
   boost::asio::awaitable<void> replace(Value value) const {
      co_await require_store()->replace(std::move(value));
   }

   template <forge::ids::typed_id_like Id, typename Fn>
   boost::asio::awaitable<void> modify(Id id, Fn&& fn) const {
      co_await require_store()->modify(id, std::forward<Fn>(fn));
   }

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<void> erase(Id id) const {
      co_await require_store()->erase(id);
   }

   template <forge::objectdb::object_model Object>
   boost::asio::awaitable<void> erase(forge::ids::object_id id) const {
      co_await require_store()->template erase<Object>(id);
   }

   template <forge::objectdb::object_model Object, typename Tag>
   [[nodiscard]] forge::objectdb::index_view<Object, Tag> index() const {
      auto state = state_;
      using value_type = typename Object::value_type;
      return forge::objectdb::index_view<Object, Tag>{
         [state](forge::objectdb::record_range range,
                 forge::objectdb::page_request request) mutable
            -> boost::asio::awaitable<forge::objectdb::object_page<value_type>> {
            auto handle = store_handle{state};
            auto view = handle.require_store()->template index<Object, Tag>();
            co_return co_await view.page(std::move(range), std::move(request));
         },
         [state]() mutable -> forge::objectdb::index_page_query<value_type> {
            auto active = std::make_shared<std::optional<forge::objectdb::snapshot>>();
            return [state = std::move(state), active](forge::objectdb::record_range range,
                                                      forge::objectdb::page_request request) mutable
                      -> boost::asio::awaitable<forge::objectdb::object_page<value_type>> {
               auto handle = store_handle{state};
               if (!active->has_value()) {
                  active->emplace(co_await handle.begin_read());
               }
               auto view = active->value().template index<Object, Tag>();
               co_return co_await view.page(std::move(range), std::move(request));
            };
         }};
   }

 private:
   [[nodiscard]] std::shared_ptr<forge::objectdb::store> require_store() const;

   std::shared_ptr<store_handle_state> state_;

   friend class api;
   friend class store_handle_state;
};

class api : public forge::api::core::contract<api, forge::api::core::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<void>
   add_store(std::string name,
             std::shared_ptr<forge::db::driver> driver,
             forge::objectdb::store::options options = {}) = 0;
   virtual boost::asio::awaitable<store_handle> store(std::string name) = 0;
   virtual boost::asio::awaitable<void> flush(std::string name, bool sync = true) = 0;
   virtual boost::asio::awaitable<void> flush_all(bool sync = true) = 0;
   virtual boost::asio::awaitable<::forge::plugins::db::objectdb::status> status() = 0;
};

} // namespace forge::plugins::db::objectdb

namespace objectdb_plugin_api = ::forge::plugins::db::objectdb;

export {
FORGE_API(objectdb_plugin_api::api, FORGE_API_CONTRACT("forge.plugins.db.objectdb", 1, 0))
}

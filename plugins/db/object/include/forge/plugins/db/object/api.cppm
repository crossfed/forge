module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

export module forge.plugins.db.object.api;

export import forge.plugins.db.object.exceptions;
export import forge.plugins.db.object.types;

import forge.api.binding;
import forge.db.core.driver;
import forge.db.core.record;
import forge.ids.object_id;
import forge.db.object.hooks;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.object.snapshot;
import forge.db.object.store;
import forge.db.object.transaction;

export namespace forge::plugins::db::object {

class store_handle_state {
 public:
   virtual ~store_handle_state() = default;

   [[nodiscard]] virtual std::string name() const = 0;
   [[nodiscard]] virtual std::shared_ptr<forge::db::object::store> require_store() const = 0;
   [[nodiscard]] virtual std::shared_ptr<forge::db::core::driver> require_driver() const = 0;
};

class store_handle {
 public:
   store_handle() = default;
   explicit store_handle(std::shared_ptr<store_handle_state> state) : state_{std::move(state)} {}

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(state_);
   }

   [[nodiscard]] std::string name() const;

   template <forge::db::object::object_model Object>
   void register_object() const {
      require_store()->template register_object<Object>();
   }

   void add_interceptor(std::shared_ptr<forge::db::object::interceptor> value) const;
   void add_observer(std::shared_ptr<forge::db::object::observer> value) const;

   boost::asio::awaitable<forge::db::object::transaction> begin_transaction() const;
   boost::asio::awaitable<forge::db::object::snapshot> begin_read() const;

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<typename forge::db::object::object_index_for_id_t<Id>::value_type> get(Id id) const {
      co_return co_await require_store()->get(id);
   }

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<std::optional<typename forge::db::object::object_index_for_id_t<Id>::value_type>>
   find(Id id) const {
      co_return co_await require_store()->find(id);
   }

   template <forge::db::object::object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id) const {
      co_return co_await require_store()->template get<Object>(id);
   }

   template <forge::db::object::object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id) const {
      co_return co_await require_store()->template find<Object>(id);
   }

   template <forge::db::object::object_value Value>
   boost::asio::awaitable<void> insert(Value value) const {
      co_await require_store()->insert(std::move(value));
   }

   template <forge::db::object::object_value Value>
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

   template <forge::db::object::object_model Object>
   boost::asio::awaitable<void> erase(forge::ids::object_id id) const {
      co_await require_store()->template erase<Object>(id);
   }

   template <forge::db::object::object_model Object, typename Tag>
   [[nodiscard]] forge::db::object::index_view<Object, Tag> index() const {
      auto state = state_;
      using value_type = typename Object::value_type;
      return forge::db::object::index_view<Object, Tag>{
         [state](forge::db::core::record_range range,
                 forge::db::core::page_request request) mutable
            -> boost::asio::awaitable<forge::db::object::object_page<value_type>> {
            auto handle = store_handle{state};
            auto view = handle.require_store()->template index<Object, Tag>();
            co_return co_await view.page(std::move(range), std::move(request));
         },
         [state]() mutable -> forge::db::object::index_page_query<value_type> {
            auto active = std::make_shared<std::optional<forge::db::object::snapshot>>();
            return [state = std::move(state), active](forge::db::core::record_range range,
                                                      forge::db::core::page_request request) mutable
                      -> boost::asio::awaitable<forge::db::object::object_page<value_type>> {
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
   [[nodiscard]] std::shared_ptr<forge::db::object::store> require_store() const;

   std::shared_ptr<store_handle_state> state_;

   friend class api;
   friend class store_handle_state;
};

class api : public forge::api::contract<api, forge::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<void>
   add_store(std::string name,
             std::shared_ptr<forge::db::core::driver> driver,
             forge::db::object::store::options options = {}) = 0;
   virtual boost::asio::awaitable<store_handle> store(std::string name) = 0;
   virtual boost::asio::awaitable<void> flush(std::string name, bool sync = true) = 0;
   virtual boost::asio::awaitable<void> flush_all(bool sync = true) = 0;
   virtual boost::asio::awaitable<::forge::plugins::db::object::status> status() = 0;
};

} // namespace forge::plugins::db::object

namespace object_plugin_api = ::forge::plugins::db::object;

export {
FORGE_API(object_plugin_api::api, FORGE_API_CONTRACT("forge.plugins.db.object", 1, 0))
}

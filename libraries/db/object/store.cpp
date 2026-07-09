module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

module forge.db.object.store;

import forge.db.core.driver;
import forge.db.core.exceptions;
import forge.db.object.exceptions;

#include "details/store_impl.hxx"

namespace forge::db::object {

store::store(std::shared_ptr<forge::db::core::driver> value, options settings)
    : store(std::move(value), config{}, settings) {}

store::store(std::shared_ptr<forge::db::core::driver> value, config settings)
    : store(std::move(value), std::move(settings), options{}) {}

store::store(std::shared_ptr<forge::db::core::driver> value, config settings, options runtime)
    : impl_{std::make_shared<impl>(std::move(value), std::move(settings), runtime)} {}

void store::add_interceptor(std::shared_ptr<interceptor> value) {
   if (value) {
      impl_->interceptors.push_back(std::move(value));
   }
}

void store::add_observer(std::shared_ptr<observer> value) {
   if (value) {
      impl_->observers.push_back(std::move(value));
   }
}

boost::asio::awaitable<transaction> store::begin_transaction() {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto ticket = std::optional<detail::write_gate::ticket>{};
   if (impl_->settings.writes == write_policy::single_writer) {
      ticket.emplace(co_await impl_->runtime->write_gate->acquire());
   }

   auto active = forge::db::core::transaction{};
   try {
      active = co_await impl_->open_write_transaction();
   } catch (const forge::db::core::exceptions::unsupported_operation&) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db object driver does not support writes");
   }
   auto release = transaction::release_fn{};
   if (ticket.has_value()) {
      auto owned_ticket = std::make_shared<std::optional<detail::write_gate::ticket>>(std::move(ticket));
      release = [owned_ticket]() mutable { owned_ticket->reset(); };
   }

   co_return transaction{
      std::move(active),
      impl_->config.family,
      [impl = impl_](forge::ids::object_id type, std::type_index model) {
         impl->ensure_registered_type(type, model);
      },
      [impl = impl_](forge::ids::object_id type,
                     forge::db::core::transaction& active) -> boost::asio::awaitable<forge::ids::object_id> {
         co_return co_await impl->allocate_id(type, active);
      },
      [impl = impl_](transaction::allocation_seal_map seals) -> boost::asio::awaitable<void> {
         co_await impl->seal_allocations(std::move(seals));
         co_return;
      },
      impl_->interceptors,
      impl_->observers,
      std::move(release),
      executor};
}

boost::asio::awaitable<snapshot> store::begin_read() {
   auto active = forge::db::core::snapshot{};
   try {
      active = co_await impl_->open_read_snapshot();
   } catch (const forge::db::core::exceptions::unsupported_operation&) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db object driver does not support snapshot reads");
   }
   co_return snapshot{
      std::move(active),
      impl_->config.family,
      [impl = impl_](forge::ids::object_id type, std::type_index model) {
         impl->ensure_registered_type(type, model);
      }};
}

transaction store::join(forge::db::core::transaction& active) {
   return transaction{
      active,
      impl_->config.family,
      [impl = impl_](forge::ids::object_id type, std::type_index model) {
         impl->ensure_registered_type(type, model);
      },
      [impl = impl_](forge::ids::object_id type,
                     forge::db::core::transaction& active) -> boost::asio::awaitable<forge::ids::object_id> {
         co_return co_await impl->allocate_id(type, active);
      },
      [impl = impl_](transaction::allocation_seal_map seals) -> boost::asio::awaitable<void> {
         co_await impl->seal_allocations(std::move(seals));
         co_return;
      },
      impl_->interceptors,
      impl_->observers};
}

void store::register_object_type(forge::ids::object_id type, std::type_index model) {
   impl_->register_object_type(type, model);
}

void store::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   impl_->ensure_registered_type(type, model);
}

} // namespace forge::db::object

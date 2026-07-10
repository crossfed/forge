module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

module forge.db.core.driver;

import forge.db.core.exceptions;

#include "details/transaction_impl.hxx"

namespace forge::db::core {

namespace {

boost::asio::awaitable<void> run_after_rollback_hooks(std::vector<transaction::after_rollback_fn> hooks) {
   for (auto& hook : hooks) {
      if (hook) {
         co_await hook();
      }
   }
   co_return;
}

boost::asio::awaitable<void> rollback_dropped_transaction(std::unique_ptr<session> active,
                                                          std::vector<transaction::after_rollback_fn> hooks) {
   try {
      co_await active->rollback();
   } catch (...) {
   }

   active.reset();

   try {
      co_await run_after_rollback_hooks(std::move(hooks));
   } catch (...) {
   }

   co_return;
}

} // namespace

transaction::impl::impl(std::unique_ptr<session> active_value, boost::asio::any_io_executor executor) noexcept
    : active{std::move(active_value)}, cleanup_executor{std::move(executor)} {}

transaction::impl::~impl() {
   rollback_on_drop();
}

boost::asio::awaitable<void> transaction::impl::run_after_rollback() {
   auto hooks = std::move(after_rollback_hooks);
   after_rollback_hooks.clear();
   co_await run_after_rollback_hooks(std::move(hooks));
}

void transaction::impl::rollback_on_drop() noexcept {
   if (!active || closed || committed) {
      active.reset();
      return;
   }

   closed = true;
   auto dropped = std::move(active);
   auto hooks = std::move(after_rollback_hooks);
   after_rollback_hooks.clear();

   try {
      boost::asio::co_spawn(cleanup_executor,
                            rollback_dropped_transaction(std::move(dropped), std::move(hooks)),
                            boost::asio::detached);
   } catch (...) {
      dropped.reset();
   }
}

transaction::transaction(std::unique_ptr<session> active, boost::asio::any_io_executor cleanup_executor)
    : impl_{std::make_shared<impl>(std::move(active), std::move(cleanup_executor))} {
   if (!impl_->active) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db transaction session is null");
   }
   const auto caps = impl_->active->capabilities();
   if (!caps.writes) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support writes");
   }
}

transaction::~transaction() = default;
transaction::transaction(transaction&&) noexcept = default;
transaction& transaction::operator=(transaction&&) noexcept = default;

bool transaction::active() const noexcept {
   return impl_ && impl_->active && !impl_->closed;
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> transaction::get(family column_family, record_key key) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   co_return co_await impl_->active->get(std::move(column_family), std::move(key));
}

boost::asio::awaitable<void> transaction::put(family column_family, record_key key, std::vector<std::byte> value) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   co_await impl_->active->put(std::move(column_family), std::move(key), std::move(value));
}

boost::asio::awaitable<void> transaction::erase(family column_family, record_key key) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   co_await impl_->active->erase(std::move(column_family), std::move(key));
}

boost::asio::awaitable<record_page> transaction::scan_page(family column_family,
                                                           record_range range,
                                                           page_request request) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db transaction is closed");
   }
   validate_page_request(request);
   co_return co_await impl_->active->scan_page(std::move(column_family), std::move(range), std::move(request));
}

void transaction::after_commit(after_commit_fn hook) {
   if (hook) {
      impl_->after_commit_hooks.push_back(std::move(hook));
   }
}

void transaction::after_rollback(after_rollback_fn hook) {
   if (hook) {
      impl_->after_rollback_hooks.push_back(std::move(hook));
   }
}

boost::asio::awaitable<void> transaction::commit() {
   if (!active()) {
      co_return;
   }

   co_await impl_->active->commit();

   auto active_session = std::move(impl_->active);
   auto after_commit_hooks = std::move(impl_->after_commit_hooks);
   impl_->after_rollback_hooks.clear();
   impl_->closed = true;
   impl_->committed = true;

   active_session.reset();
   for (auto& hook : after_commit_hooks) {
      if (hook) {
         co_await hook();
      }
   }
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!active()) {
      co_return;
   }

   auto active_session = std::move(impl_->active);
   auto hooks = std::move(impl_->after_rollback_hooks);
   impl_->after_commit_hooks.clear();
   impl_->after_rollback_hooks.clear();
   impl_->closed = true;

   auto error = std::exception_ptr{};
   try {
      co_await active_session->rollback();
   } catch (...) {
      error = std::current_exception();
   }
   active_session.reset();
   for (auto& hook : hooks) {
      if (hook) {
         try {
            co_await hook();
         } catch (...) {
            if (!error) {
               error = std::current_exception();
            }
         }
      }
   }
   if (error) {
      std::rethrow_exception(error);
   }
}

} // namespace forge::db::core

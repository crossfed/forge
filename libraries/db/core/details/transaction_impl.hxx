#pragma once

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace forge::db::core {

struct transaction::impl {
   explicit impl(std::unique_ptr<session> active_value, boost::asio::any_io_executor executor) noexcept;
   ~impl();

   void rollback_on_drop() noexcept;
   void run_after_rollback() noexcept;

   std::unique_ptr<session> active;
   boost::asio::any_io_executor cleanup_executor;
   std::vector<after_commit_fn> after_commit_hooks;
   std::vector<after_rollback_fn> after_rollback_hooks;
   bool closed = false;
   bool committed = false;
};

} // namespace forge::db::core

#pragma once

namespace forge::objectdb {

struct transaction::impl {
   impl(forge::db::transaction active_value,
        forge::db::family family_value,
        transaction::ensure_registered_fn ensure,
        std::vector<std::shared_ptr<interceptor>> interceptors_value,
        std::vector<std::shared_ptr<observer>> observers_value,
        transaction::release_fn release) noexcept;

   impl(forge::db::transaction& active_value,
        forge::db::family family_value,
        transaction::ensure_registered_fn ensure,
        std::vector<std::shared_ptr<interceptor>> interceptors_value,
        std::vector<std::shared_ptr<observer>> observers_value) noexcept;

   std::optional<forge::db::transaction> owned;
   forge::db::transaction* active = nullptr;
   forge::db::family family;
   transaction::ensure_registered_fn ensure_registered;
   std::vector<std::shared_ptr<interceptor>> interceptors;
   std::vector<std::shared_ptr<observer>> observers;
   transaction::release_fn release_writer;
   change_set changes;
   bool owns_commit = false;
   bool finalized = false;

   void release() noexcept;
   void after_rollback() noexcept;
   boost::asio::awaitable<void> after_commit();
};

} // namespace forge::objectdb

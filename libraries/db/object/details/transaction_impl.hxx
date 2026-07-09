#pragma once

#include <boost/asio/awaitable.hpp>

#include <map>

namespace forge::db::object {

struct transaction::impl {
   impl(forge::db::core::transaction active_value,
        forge::db::core::family family_value,
        transaction::ensure_registered_fn ensure,
        transaction::allocate_id_fn allocate,
        transaction::seal_allocations_fn seal,
        std::vector<std::shared_ptr<interceptor>> interceptors_value,
        std::vector<std::shared_ptr<observer>> observers_value,
        transaction::release_fn release) noexcept;

   impl(forge::db::core::transaction& active_value,
        forge::db::core::family family_value,
        transaction::ensure_registered_fn ensure,
        transaction::allocate_id_fn allocate,
        transaction::seal_allocations_fn seal,
        std::vector<std::shared_ptr<interceptor>> interceptors_value,
        std::vector<std::shared_ptr<observer>> observers_value) noexcept;

   std::optional<forge::db::core::transaction> owned;
   forge::db::core::transaction* active = nullptr;
   forge::db::core::family family;
   transaction::ensure_registered_fn ensure_registered;
   transaction::allocate_id_fn allocate_id;
   transaction::seal_allocations_fn seal_allocations;
   transaction::allocation_seal_map allocation_seals;
   std::vector<std::shared_ptr<interceptor>> interceptors;
   std::vector<std::shared_ptr<observer>> observers;
   transaction::release_fn release_writer;
   change_set changes;
   bool owns_commit = false;
   bool finalized = false;

   void release() noexcept;
   void remember_allocation(forge::ids::object_id type, std::uint64_t next_instance);
   boost::asio::awaitable<void> after_rollback();
   boost::asio::awaitable<void> after_commit();
};

} // namespace forge::db::object

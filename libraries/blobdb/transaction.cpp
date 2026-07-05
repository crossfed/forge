module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module forge.blobdb.transaction;

import forge.blobdb.exceptions;
import forge.db.exceptions;

#include "details/key_codec.hxx"
#include "details/transaction_impl.hxx"

namespace forge::blobdb {

namespace {

void require_digest(const digest& id) {
   if (id.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blob digest must not be empty");
   }
}

void require_owner(const owner_ref& owner) {
   if (owner.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blob owner reference must not be empty");
   }
}

digest digest_from_data_key(const forge::db::record_key& key) {
   const auto& bytes = key.bytes();
   if (bytes.size() < 2U || bytes.front() != static_cast<std::byte>(0x10U)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blobdb data key is malformed");
   }
   return digest{std::vector<std::byte>{bytes.begin() + 1, bytes.end()}};
}

} // namespace

transaction::impl::impl(owned_tag,
                        forge::db::transaction active_value,
                        forge::db::family data,
                        forge::db::family refs,
                        std::shared_ptr<hasher> value_hasher,
                        bool verify_writes,
                        bool verify_reads) noexcept
    : owned{std::move(active_value)},
      active{&*owned},
      data_family{std::move(data)},
      refs_family{std::move(refs)},
      digest_hasher{std::move(value_hasher)},
      verify_on_write{verify_writes},
      verify_on_read{verify_reads},
      owns_commit{true} {}

transaction::impl::impl(borrowed_tag,
                        forge::db::transaction& active_value,
                        forge::db::family data,
                        forge::db::family refs,
                        std::shared_ptr<hasher> value_hasher,
                        bool verify_writes,
                        bool verify_reads) noexcept
    : active{&active_value},
      data_family{std::move(data)},
      refs_family{std::move(refs)},
      digest_hasher{std::move(value_hasher)},
      verify_on_write{verify_writes},
      verify_on_read{verify_reads} {}

forge::db::transaction& transaction::impl::transaction() {
   if (active == nullptr || !active->active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "blobdb transaction is closed");
   }
   return *active;
}

transaction::transaction(forge::db::transaction&& active,
                         forge::db::family data_family,
                         forge::db::family refs_family,
                         std::shared_ptr<hasher> digest_hasher,
                         bool verify_writes,
                         bool verify_reads)
    : impl_{std::make_shared<impl>(
         impl::owned_tag{},
         std::move(active),
         std::move(data_family),
         std::move(refs_family),
         std::move(digest_hasher),
         verify_writes,
         verify_reads)} {}

transaction::transaction(forge::db::transaction& active,
                         forge::db::family data_family,
                         forge::db::family refs_family,
                         std::shared_ptr<hasher> digest_hasher,
                         bool verify_writes,
                         bool verify_reads)
    : impl_{std::make_shared<impl>(
         impl::borrowed_tag{},
         active,
         std::move(data_family),
         std::move(refs_family),
         std::move(digest_hasher),
         verify_writes,
         verify_reads)} {}

forge::db::transaction& transaction::db_transaction() const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "blobdb transaction is closed");
   }
   return impl_->transaction();
}

boost::asio::awaitable<digest> transaction::put(std::vector<std::byte> bytes) {
   if (!impl_ || !impl_->digest_hasher) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "blobdb hasher is required for digest-from-bytes put");
   }
   auto id = impl_->digest_hasher->hash(bytes);
   co_await put(id, std::move(bytes));
   co_return id;
}

boost::asio::awaitable<void> transaction::put(digest id, std::vector<std::byte> bytes) {
   require_digest(id);
   if (impl_->verify_on_write && impl_->digest_hasher) {
      const auto actual = impl_->digest_hasher->hash(bytes);
      if (actual != id) {
         FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob digest does not match payload on write");
      }
   }
   co_await impl_->transaction().put(impl_->data_family, detail::data_key(id), std::move(bytes));
}

boost::asio::awaitable<std::vector<std::byte>> transaction::get(digest id) {
   require_digest(id);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(id));
   if (!bytes.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "blob was not found");
   }
   if (impl_->verify_on_read && impl_->digest_hasher) {
      const auto actual = impl_->digest_hasher->hash(*bytes);
      if (actual != id) {
         FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob digest does not match payload on read");
      }
   }
   co_return *bytes;
}

boost::asio::awaitable<bool> transaction::has(digest id) {
   require_digest(id);
   co_return (co_await impl_->transaction().get(impl_->data_family, detail::data_key(id))).has_value();
}

boost::asio::awaitable<stat> transaction::stat_blob(digest id) {
   require_digest(id);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(id));
   if (!bytes.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "blob was not found");
   }
   co_return stat{
      .id = std::move(id),
      .size = static_cast<std::uint64_t>(bytes->size()),
      .refs = co_await ref_count(id),
   };
}

boost::asio::awaitable<void> transaction::erase(digest id) {
   require_digest(id);
   co_await impl_->transaction().erase(impl_->data_family, detail::data_key(id));
}

boost::asio::awaitable<void> transaction::verify(digest id) {
   static_cast<void>(co_await get(std::move(id)));
}

boost::asio::awaitable<void> transaction::retain(digest id, owner_ref owner) {
   require_digest(id);
   require_owner(owner);
   co_await impl_->transaction().put(
      impl_->refs_family,
      detail::ref_key(id, owner),
      std::vector<std::byte>{static_cast<std::byte>(1U)});
}

boost::asio::awaitable<void> transaction::release(digest id, owner_ref owner) {
   require_digest(id);
   require_owner(owner);
   co_await impl_->transaction().erase(impl_->refs_family, detail::ref_key(id, owner));
}

boost::asio::awaitable<std::uint64_t> transaction::ref_count(digest id) {
   require_digest(id);
   auto count = std::uint64_t{};
   auto request = forge::db::page_request{.limit = 100};
   const auto prefix = detail::ref_prefix(id);
   while (true) {
      auto page = co_await impl_->transaction().scan_page(
         impl_->refs_family,
         forge::db::record_range{.begin = prefix, .prefix = prefix, .has_end = false},
         request);
      count += page.entries.size();
      if (!page.next.has_value()) {
         break;
      }
      request.after = std::move(page.next);
   }
   co_return count;
}

boost::asio::awaitable<collect_result> transaction::collect_unreferenced(collect_options options) {
   auto result = collect_result{};
   if (options.limit == 0) {
      co_return result;
   }

   auto request = forge::db::page_request{.limit = 100};
   const auto prefix = detail::data_prefix();
   while (result.removed < options.limit) {
      auto page = co_await impl_->transaction().scan_page(
         impl_->data_family,
         forge::db::record_range{.begin = prefix, .prefix = prefix, .has_end = false},
         request);
      for (const auto& entry : page.entries) {
         auto id = digest_from_data_key(entry.key);
         if ((co_await ref_count(id)) == 0) {
            co_await erase(std::move(id));
            ++result.removed;
            if (result.removed >= options.limit) {
               break;
            }
         }
      }
      if (!page.next.has_value() || result.removed >= options.limit) {
         break;
      }
      request.after = std::move(page.next);
   }
   co_return result;
}

boost::asio::awaitable<void> transaction::commit() {
   if (!impl_ || !impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined blobdb transaction does not own commit");
   }
   co_await impl_->transaction().commit();
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!impl_ || !impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined blobdb transaction does not own rollback");
   }
   co_await impl_->transaction().rollback();
}

} // namespace forge::blobdb

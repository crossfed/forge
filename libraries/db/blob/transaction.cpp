module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module forge.db.blob.transaction;

import forge.db.blob.exceptions;
import forge.db.core.exceptions;

#include "details/key_codec.hxx"
#include "details/transaction_impl.hxx"

namespace forge::db::blob {

namespace {

void require_encoded_ref(const std::string& algorithm, const std::vector<std::byte>& digest) {
   if (algorithm.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blob digest algorithm must not be empty");
   }
   if (digest.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blob digest must not be empty");
   }
}

void require_owner(const owner_ref& owner) {
   if (owner.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "blob owner reference must not be empty");
   }
}

detail::encoded_ref ref_from_data_key(const forge::db::core::record_key& key) {
   auto value = detail::ref_from_data_key(key);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db blob data key is malformed");
   }
   return std::move(*value);
}

} // namespace

transaction::impl::impl(owned_tag,
                        forge::db::core::transaction active_value,
                        forge::db::core::family data,
                        forge::db::core::family refs) noexcept
    : owned{std::move(active_value)},
      active{&*owned},
      data_family{std::move(data)},
      refs_family{std::move(refs)},
      owns_commit{true} {}

transaction::impl::impl(borrowed_tag,
                        forge::db::core::transaction& active_value,
                        forge::db::core::family data,
                        forge::db::core::family refs) noexcept
    : active{&active_value},
      data_family{std::move(data)},
      refs_family{std::move(refs)} {}

forge::db::core::transaction& transaction::impl::transaction() {
   if (active == nullptr || !active->active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db blob transaction is closed");
   }
   return *active;
}

transaction::transaction(forge::db::core::transaction&& active,
                         forge::db::core::family data_family,
                         forge::db::core::family refs_family)
    : impl_{std::make_shared<impl>(
         impl::owned_tag{},
         std::move(active),
         std::move(data_family),
         std::move(refs_family))} {}

transaction::transaction(forge::db::core::transaction& active,
                         forge::db::core::family data_family,
                         forge::db::core::family refs_family)
    : impl_{std::make_shared<impl>(
         impl::borrowed_tag{},
         active,
         std::move(data_family),
         std::move(refs_family))} {}

forge::db::core::transaction& transaction::db_transaction() const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db blob transaction is closed");
   }
   return impl_->transaction();
}

boost::asio::awaitable<ref<digest>> transaction::put(std::vector<std::byte> payload) {
   co_return co_await put_as<digest>(std::move(payload));
}

boost::asio::awaitable<void> transaction::put_encoded(std::string algorithm,
                                                      std::vector<std::byte> digest,
                                                      std::uint64_t,
                                                      std::vector<std::byte> payload) {
   require_encoded_ref(algorithm, digest);
   co_await impl_->transaction().put(impl_->data_family, detail::data_key(algorithm, digest), std::move(payload));
}

boost::asio::awaitable<std::vector<std::byte>> transaction::get_encoded(std::string algorithm,
                                                                        std::vector<std::byte> digest,
                                                                        std::uint64_t size) {
   require_encoded_ref(algorithm, digest);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(algorithm, digest));
   if (!bytes.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "blob was not found");
   }
   if (bytes->size() != size) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob size does not match reference");
   }
   co_return *bytes;
}

boost::asio::awaitable<bool> transaction::has_encoded(std::string algorithm,
                                                      std::vector<std::byte> digest,
                                                      std::uint64_t size) {
   require_encoded_ref(algorithm, digest);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(algorithm, digest));
   co_return bytes.has_value() && bytes->size() == size;
}

boost::asio::awaitable<stat> transaction::stat_blob_encoded(std::string algorithm,
                                                            std::vector<std::byte> digest,
                                                            std::uint64_t size) {
   require_encoded_ref(algorithm, digest);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(algorithm, digest));
   if (!bytes.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "blob was not found");
   }
   if (bytes->size() != size) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob size does not match reference");
   }
   co_return stat{
      .size = static_cast<std::uint64_t>(bytes->size()),
      .refs = co_await ref_count_encoded(std::move(algorithm), std::move(digest)),
   };
}

boost::asio::awaitable<void> transaction::erase_encoded(std::string algorithm,
                                                        std::vector<std::byte> digest,
                                                        std::uint64_t size) {
   require_encoded_ref(algorithm, digest);
   auto key = detail::data_key(algorithm, digest);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, key);
   if (!bytes.has_value()) {
      co_return;
   }
   if (bytes->size() != size) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob size does not match reference");
   }
   co_await impl_->transaction().erase(impl_->data_family, std::move(key));
}

boost::asio::awaitable<void> transaction::erase_stored_encoded(std::string algorithm, std::vector<std::byte> digest) {
   require_encoded_ref(algorithm, digest);
   co_await impl_->transaction().erase(impl_->data_family, detail::data_key(algorithm, digest));
}

boost::asio::awaitable<void> transaction::retain_encoded(std::string algorithm,
                                                         std::vector<std::byte> digest,
                                                         std::uint64_t size,
                                                         owner_ref owner) {
   require_encoded_ref(algorithm, digest);
   require_owner(owner);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(algorithm, digest));
   if (!bytes.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "blob was not found");
   }
   if (bytes->size() != size) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob size does not match reference");
   }
   co_await impl_->transaction().put(
      impl_->refs_family,
      detail::ref_key(algorithm, digest, owner),
      std::vector<std::byte>{static_cast<std::byte>(1U)});
}

boost::asio::awaitable<void> transaction::release_encoded(std::string algorithm,
                                                          std::vector<std::byte> digest,
                                                          std::uint64_t size,
                                                          owner_ref owner) {
   require_encoded_ref(algorithm, digest);
   require_owner(owner);
   auto bytes = co_await impl_->transaction().get(impl_->data_family, detail::data_key(algorithm, digest));
   if (!bytes.has_value()) {
      co_return;
   }
   if (bytes->size() != size) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob size does not match reference");
   }
   co_await impl_->transaction().erase(impl_->refs_family, detail::ref_key(algorithm, digest, owner));
}

boost::asio::awaitable<std::uint64_t> transaction::ref_count_encoded(std::string algorithm, std::vector<std::byte> digest) {
   require_encoded_ref(algorithm, digest);
   auto count = std::uint64_t{};
   auto request = forge::db::core::page_request{.limit = 100};
   const auto prefix = detail::ref_prefix(algorithm, digest);
   while (true) {
      auto page = co_await impl_->transaction().scan_page(
         impl_->refs_family,
         forge::db::core::record_range{.begin = prefix, .prefix = prefix, .has_end = false},
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

   auto request = forge::db::core::page_request{.limit = 100};
   const auto prefix = detail::data_prefix();
   while (result.removed < options.limit) {
      auto page = co_await impl_->transaction().scan_page(
         impl_->data_family,
         forge::db::core::record_range{.begin = prefix, .prefix = prefix, .has_end = false},
         request);
      for (const auto& entry : page.entries) {
         auto key_ref = ref_from_data_key(entry.key);
         if ((co_await ref_count_encoded(key_ref.algorithm, key_ref.digest)) == 0) {
            co_await erase_stored_encoded(std::move(key_ref.algorithm), std::move(key_ref.digest));
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
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined db blob transaction does not own commit");
   }
   co_await impl_->transaction().commit();
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!impl_ || !impl_->owns_commit) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "joined db blob transaction does not own rollback");
   }
   co_await impl_->transaction().rollback();
}

} // namespace forge::db::blob

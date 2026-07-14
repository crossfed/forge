module;

#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>

export module forge.db.blob.store;

import forge.db.blob.ref;
import forge.db.blob.snapshot;
import forge.db.blob.transaction;
import forge.db.blob.types;
import forge.db.core.driver;
import forge.db.core.record;

export namespace forge::db::blob {

class store {
 public:
   struct config {
      forge::db::core::family data_family{"blobdb.data"};
      forge::db::core::family refs_family{"blobdb.refs"};
   };

   explicit store(std::shared_ptr<forge::db::core::driver> driver);
   store(std::shared_ptr<forge::db::core::driver> driver, config settings);

   boost::asio::awaitable<transaction> begin_transaction();
   boost::asio::awaitable<snapshot> begin_read();
   [[nodiscard]] snapshot join(const forge::db::core::snapshot& active);
   [[nodiscard]] transaction join(forge::db::core::transaction& active);

   template <typename SharedTransaction>
      requires requires(SharedTransaction& active) {
         { active.db_transaction() } -> std::same_as<forge::db::core::transaction&>;
      }
   [[nodiscard]] transaction join(SharedTransaction& active) {
      return join(active.db_transaction());
   }

   boost::asio::awaitable<ref<digest>> put(std::vector<std::byte> payload);

   template <digest_algorithm Digest>
   boost::asio::awaitable<ref<Digest>> put_as(std::vector<std::byte> payload) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      auto value = std::optional<ref<Digest>>{};
      try {
         value = co_await active.template put_as<Digest>(std::move(payload));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
      co_return std::move(*value);
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> put(ref<Digest> value, std::vector<std::byte> payload) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      try {
         co_await active.put(std::move(value), std::move(payload));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<std::vector<std::byte>> get(ref<Digest> value) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      auto bytes = std::vector<std::byte>{};
      try {
         bytes = co_await active.get(std::move(value));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
      co_return bytes;
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<bool> has(ref<Digest> value) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      auto exists = false;
      try {
         exists = co_await active.has(std::move(value));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
      co_return exists;
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<stat> stat_blob(ref<Digest> value) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      auto result = stat{};
      try {
         result = co_await active.stat_blob(std::move(value));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
      co_return result;
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> erase(ref<Digest> value) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      try {
         co_await active.erase(std::move(value));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> verify(ref<Digest> value) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      try {
         co_await active.verify(std::move(value));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> retain(ref<Digest> value, owner_ref owner) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      try {
         co_await active.retain(std::move(value), std::move(owner));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> release(ref<Digest> value, owner_ref owner) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      try {
         co_await active.release(std::move(value), std::move(owner));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<std::uint64_t> ref_count(ref<Digest> value) {
      auto active = co_await begin_transaction();
      auto error = std::exception_ptr{};
      auto count = std::uint64_t{};
      try {
         count = co_await active.ref_count(std::move(value));
         co_await active.commit();
      } catch (...) {
         error = std::current_exception();
      }
      if (error) {
         co_await active.rollback();
         std::rethrow_exception(error);
      }
      co_return count;
   }

   boost::asio::awaitable<collect_result> collect_unreferenced(collect_options options = {});

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::db::blob

module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

module forge.contract.testing.host;

import forge.db.core.driver;
import forge.db.core.record;

#include "details/memory_driver.hxx"

namespace forge::contract::testing {

namespace {

struct byte_less {
   bool operator()(const forge::db::core::record_key& left, const forge::db::core::record_key& right) const noexcept {
      return left.bytes() < right.bytes();
   }
};

using records = std::map<forge::db::core::record_key, std::vector<std::byte>, byte_less>;
using families = std::map<std::string, records>;

void append_size(std::vector<std::uint8_t>& output, std::size_t value) {
   for (auto shift = 0U; shift < 64U; shift += 8U) {
      output.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >> shift) & 0xffU));
   }
}

template <class Range> void append_bytes(std::vector<std::uint8_t>& output, const Range& value) {
   append_size(output, value.size());
   for (const auto byte : value) {
      if constexpr (std::is_same_v<std::remove_cv_t<decltype(byte)>, std::byte>) {
         output.push_back(std::to_integer<std::uint8_t>(byte));
      } else {
         output.push_back(static_cast<std::uint8_t>(byte));
      }
   }
}

class session final : public forge::db::core::session {
 public:
   session(std::shared_ptr<memory_driver::state> state, bool writable);

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::core::family family,
                                                                     forge::db::core::record_key key) override;
   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(forge::db::core::family family, forge::db::core::record_key key) override;
   boost::asio::awaitable<void> put(forge::db::core::family family, forge::db::core::record_key key,
                                    std::vector<std::byte> value) override;
   boost::asio::awaitable<void> erase(forge::db::core::family family, forge::db::core::record_key key) override;
   boost::asio::awaitable<forge::db::core::record_page> scan_page(forge::db::core::family family,
                                                                  forge::db::core::record_range range,
                                                                  forge::db::core::page_request request) override;
   boost::asio::awaitable<void> commit() override;
   boost::asio::awaitable<void> rollback() override;

 private:
   std::shared_ptr<memory_driver::state> state_;
   families records_;
   bool writable_ = false;
   bool active_ = true;
};

} // namespace

struct memory_driver::state {
   std::mutex mutex;
   families records;
};

namespace {

session::session(std::shared_ptr<memory_driver::state> state, bool writable)
    : state_{std::move(state)}, writable_{writable} {
   const auto lock = std::scoped_lock{state_->mutex};
   records_ = state_->records;
}

forge::db::core::capabilities session::capabilities() const noexcept {
   return forge::db::core::capabilities{
       .snapshot_reads = !writable_,
       .writes = writable_,
       .savepoints = false,
       .record_locks = writable_,
   };
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> session::get(forge::db::core::family family,
                                                                           forge::db::core::record_key key) {
   const auto family_it = records_.find(family.name);
   if (family_it == records_.end()) {
      co_return std::nullopt;
   }
   const auto found = family_it->second.find(key);
   if (found == family_it->second.end()) {
      co_return std::nullopt;
   }
   co_return found->second;
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> session::get_for_update(forge::db::core::family family,
                                                                                      forge::db::core::record_key key) {
   co_return co_await get(std::move(family), std::move(key));
}

boost::asio::awaitable<void> session::put(forge::db::core::family family, forge::db::core::record_key key,
                                          std::vector<std::byte> value) {
   if (!writable_ || !active_) {
      throw std::logic_error{"contract test memory session is not writable"};
   }
   records_[family.name][std::move(key)] = std::move(value);
   co_return;
}

boost::asio::awaitable<void> session::erase(forge::db::core::family family, forge::db::core::record_key key) {
   if (!writable_ || !active_) {
      throw std::logic_error{"contract test memory session is not writable"};
   }
   if (auto family_it = records_.find(family.name); family_it != records_.end()) {
      family_it->second.erase(key);
   }
   co_return;
}

boost::asio::awaitable<forge::db::core::record_page> session::scan_page(forge::db::core::family family,
                                                                        forge::db::core::record_range range,
                                                                        forge::db::core::page_request request) {
   forge::db::core::validate_page_request(request);
   auto result = forge::db::core::record_page{};
   const auto family_it = records_.find(family.name);
   if (family_it == records_.end()) {
      co_return result;
   }

   const auto& source = family_it->second;
   auto current = source.lower_bound(request.after ? request.after->boundary : range.begin);
   if (request.after && current != source.end() && current->first == request.after->boundary) {
      ++current;
   }

   auto last = std::optional<forge::db::core::record_key>{};
   while (current != source.end() && (!range.has_end || current->first < range.end)) {
      result.entries.push_back({.key = current->first, .value = current->second});
      last = current->first;
      ++current;
      if (result.entries.size() == request.limit) {
         break;
      }
   }
   if (last && current != source.end() && (!range.has_end || current->first < range.end)) {
      result.next = forge::db::core::cursor{.boundary = std::move(*last)};
   }
   co_return result;
}

boost::asio::awaitable<void> session::commit() {
   if (!writable_ || !active_) {
      throw std::logic_error{"contract test memory session cannot commit"};
   }
   {
      const auto lock = std::scoped_lock{state_->mutex};
      state_->records = std::move(records_);
   }
   active_ = false;
   co_return;
}

boost::asio::awaitable<void> session::rollback() {
   active_ = false;
   records_.clear();
   co_return;
}

} // namespace

memory_driver::memory_driver() : state_{std::make_shared<state>()} {}

memory_driver::~memory_driver() = default;

std::vector<std::uint8_t> memory_driver::snapshot() const {
   const auto lock = std::scoped_lock{state_->mutex};
   auto result = std::vector<std::uint8_t>{};
   append_size(result, state_->records.size());
   for (const auto& [family, records] : state_->records) {
      append_bytes(result, family);
      append_size(result, records.size());
      for (const auto& [key, value] : records) {
         append_bytes(result, key.bytes());
         append_bytes(result, value);
      }
   }
   return result;
}

boost::asio::awaitable<void> memory_driver::async_flush(bool) {
   co_return;
}

boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> memory_driver::open_transaction() {
   co_return std::make_unique<session>(state_, true);
}

boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> memory_driver::open_snapshot() {
   co_return std::make_unique<session>(state_, false);
}

} // namespace forge::contract::testing

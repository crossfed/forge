module;

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <forge/exceptions/macros.hpp>

export module forge.db.core.record;

import forge.db.core.exceptions;

export namespace forge::db::core {

inline constexpr std::uint32_t default_page_limit = 100;
inline constexpr std::uint32_t max_page_limit = 10'000;

class family final {
 public:
   family() = default;

   explicit family(std::string value) : name{std::move(value)} {
      if (name.empty()) {
         throw std::invalid_argument{"DB family name must not be empty"};
      }
   }

   std::string name = "default";
};

class record_key {
 public:
   record_key() = default;

   explicit record_key(std::vector<std::byte> bytes) : _bytes(std::move(bytes)) {}

   [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept {
      return _bytes;
   }

   [[nodiscard]] bool empty() const noexcept {
      return _bytes.empty();
   }

   friend bool operator==(const record_key&, const record_key&) = default;
   friend auto operator<=>(const record_key&, const record_key&) = default;

 private:
   std::vector<std::byte> _bytes;
};

struct record_range {
   record_key begin;
   record_key end;
   record_key prefix;
   bool has_end = true;
};

struct record_entry {
   record_key key;
   std::vector<std::byte> value;
};

struct cursor {
   record_key boundary;

   bool operator==(const cursor&) const = default;
};

struct page_request {
   std::optional<cursor> after;
   std::uint32_t limit = default_page_limit;
};

struct record_page {
   std::vector<record_entry> entries;
   std::optional<cursor> next;
};

inline void validate_page_request(const page_request& request) {
   if (request.limit == 0 || request.limit > max_page_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_cursor, "invalid db page limit",
                            forge::exceptions::ctx("limit", request.limit),
                            forge::exceptions::ctx("max", max_page_limit));
   }
}

} // namespace forge::db::core

module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module forge.blobdb.types;

import forge.db.record;

export namespace forge::blobdb {

class digest final {
 public:
   digest() = default;
   explicit digest(std::vector<std::byte> value) : bytes{std::move(value)} {}

   [[nodiscard]] bool empty() const noexcept {
      return bytes.empty();
   }

   friend bool operator==(const digest&, const digest&) = default;
   friend auto operator<=>(const digest&, const digest&) = default;

   std::vector<std::byte> bytes;
};

class owner_ref final {
 public:
   owner_ref() = default;
   explicit owner_ref(std::vector<std::byte> value) : bytes{std::move(value)} {}
   explicit owner_ref(std::string value)
       : bytes(reinterpret_cast<const std::byte*>(value.data()),
               reinterpret_cast<const std::byte*>(value.data() + value.size())) {}

   [[nodiscard]] bool empty() const noexcept {
      return bytes.empty();
   }

   friend bool operator==(const owner_ref&, const owner_ref&) = default;
   friend auto operator<=>(const owner_ref&, const owner_ref&) = default;

   std::vector<std::byte> bytes;
};

struct stat {
   digest id;
   std::uint64_t size = 0;
   std::uint64_t refs = 0;
};

struct collect_result {
   std::uint64_t removed = 0;
};

struct collect_options {
   std::uint64_t limit = 100;
};

class hasher {
 public:
   virtual ~hasher() = default;
   virtual digest hash(std::span<const std::byte> bytes) const = 0;
};

} // namespace forge::blobdb

export namespace forge::blobdb {

BOOST_DESCRIBE_STRUCT(digest, (), (bytes))
BOOST_DESCRIBE_STRUCT(owner_ref, (), (bytes))
BOOST_DESCRIBE_STRUCT(stat, (), (id, size, refs))
BOOST_DESCRIBE_STRUCT(collect_result, (), (removed))
BOOST_DESCRIBE_STRUCT(collect_options, (), (limit))

} // namespace forge::blobdb

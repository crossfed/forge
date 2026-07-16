module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module forge.db.blob.types;

import forge.crypto.sha256;
import forge.db.core.record;
import forge.db.ids.object_id;

export namespace forge::db::blob {

using digest = forge::crypto::sha256;

class owner_ref final {
 public:
   owner_ref() = default;
   explicit owner_ref(std::vector<std::byte> value) : bytes{std::move(value)} {}
   explicit owner_ref(std::string value)
       : bytes(reinterpret_cast<const std::byte*>(value.data()),
               reinterpret_cast<const std::byte*>(value.data() + value.size())) {}
   explicit owner_ref(forge::db::ids::object_id value);

   template <forge::db::ids::typed_id_like Id>
   explicit owner_ref(Id value) : owner_ref{value.as_object_id()} {}

   [[nodiscard]] bool empty() const noexcept {
      return bytes.empty();
   }

   friend bool operator==(const owner_ref&, const owner_ref&) = default;
   friend auto operator<=>(const owner_ref&, const owner_ref&) = default;

   std::vector<std::byte> bytes;
};

struct stat {
   std::uint64_t size = 0;
   std::uint64_t refs = 0;
};

struct collect_result {
   std::uint64_t removed = 0;
};

struct collect_options {
   std::uint64_t limit = 100;
};

} // namespace forge::db::blob

export namespace forge::db::blob {

BOOST_DESCRIBE_STRUCT(owner_ref, (), (bytes))
BOOST_DESCRIBE_STRUCT(stat, (), (size, refs))
BOOST_DESCRIBE_STRUCT(collect_result, (), (removed))
BOOST_DESCRIBE_STRUCT(collect_options, (), (limit))

} // namespace forge::db::blob

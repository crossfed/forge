#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace forge::db::object::detail::ranked_index {

using bytes = std::vector<std::byte>;
inline constexpr auto level_count = std::uint8_t{16};

enum class scalar_kind : std::uint8_t {
   signed_value = 1,
   unsigned_value = 2,
};

enum class error_code : std::uint8_t {
   rebuild_required,
   corruption,
   overflow,
};

class error final : public std::runtime_error {
 public:
   error(error_code value, std::string message)
       : std::runtime_error{std::move(message)}, code{value} {}

   error_code code;
};

struct aggregate {
   std::uint64_t count = 0;
   std::vector<std::uint64_t> sums;
};

struct rank_result {
   std::uint64_t lower = 0;
   std::uint64_t upper = 0;
   std::uint64_t size = 0;
};

struct layout {
   bytes root;
   std::vector<bytes> levels;
   bytes source_prefix;
   bytes object_prefix;
   bytes schema;
   std::vector<scalar_kind> sum_kinds;
};

struct bounds {
   bytes lower;
   std::optional<bytes> upper;
   bool lower_at_end = false;
};

struct record {
   bytes key;
   bytes value;
};

struct read_access {
   std::function<boost::asio::awaitable<std::optional<bytes>>(bytes)> get;
   std::function<boost::asio::awaitable<std::optional<record>>(bytes, bytes)> next;
   std::function<boost::asio::awaitable<bool>(bytes)> has_any;
};

struct write_access : read_access {
   std::function<boost::asio::awaitable<std::optional<bytes>>(bytes)> lock;
   std::function<boost::asio::awaitable<void>(bytes, bytes)> put;
   std::function<boost::asio::awaitable<void>(bytes)> erase;
};

struct entry {
   bytes key;
   aggregate contribution;
};

struct mutation_plan {
   std::vector<std::pair<bytes, bytes>> puts;
   std::vector<bytes> erases;
};

[[nodiscard]] bounds bounds_from_source_range(const layout& descriptor,
                                               const bytes& begin,
                                               const std::optional<bytes>& end);
[[nodiscard]] bytes source_key(const layout& descriptor, const bytes& logical);

boost::asio::awaitable<void> lock_root(const write_access& access, const layout& descriptor);
boost::asio::awaitable<aggregate> query(const read_access& access, const layout& descriptor,
                                        const bounds& range);
boost::asio::awaitable<rank_result>
query_ranks(const read_access& access, const layout& descriptor, const bounds& range);
boost::asio::awaitable<std::optional<bytes>>
nth_key(const read_access& access, const layout& descriptor, std::uint64_t position);
boost::asio::awaitable<mutation_plan>
plan_change(const write_access& access, const layout& descriptor,
            std::optional<entry> before, std::optional<entry> after);
boost::asio::awaitable<void> apply(const write_access& access, mutation_plan plan);

} // namespace forge::db::object::detail::ranked_index

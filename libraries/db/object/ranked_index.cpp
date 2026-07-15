#include "details/ranked_index.hxx"

#include <boost/crc.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace forge::db::object::detail::ranked_index {

namespace {

constexpr auto root_magic = std::array<std::byte, 4>{
   static_cast<std::byte>('F'), static_cast<std::byte>('R'),
   static_cast<std::byte>('A'), static_cast<std::byte>('1')};
constexpr auto format_version = std::uint8_t{2};
constexpr auto crc64_ecma = std::uint8_t{1};

using signed_wide = boost::multiprecision::int128_t;

void append_u32(bytes& out, std::uint32_t value) {
   for (auto shift = 24; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

void append_u64(bytes& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

std::uint32_t read_u32(std::span<const std::byte> input, std::size_t& offset) {
   if (input.size() - offset < sizeof(std::uint32_t)) {
      throw error{error_code::corruption, "ranked index value is truncated"};
   }
   auto result = std::uint32_t{0};
   for (auto index = 0U; index < sizeof(std::uint32_t); ++index) {
      result = static_cast<std::uint32_t>((result << 8U) |
         std::to_integer<std::uint8_t>(input[offset++]));
   }
   return result;
}

std::uint64_t read_u64(std::span<const std::byte> input, std::size_t& offset) {
   if (input.size() - offset < sizeof(std::uint64_t)) {
      throw error{error_code::corruption, "ranked index value is truncated"};
   }
   auto result = std::uint64_t{0};
   for (auto index = 0U; index < sizeof(std::uint64_t); ++index) {
      result = (result << 8U) | std::to_integer<std::uint8_t>(input[offset++]);
   }
   return result;
}

aggregate zero(const layout& descriptor) {
   return aggregate{.count = 0, .sums = std::vector<std::uint64_t>(descriptor.sum_kinds.size())};
}

std::uint64_t encode_scalar(scalar_kind kind, const signed_wide& value, bool underflow_is_corruption) {
   if (kind == scalar_kind::signed_value) {
      if (value < std::numeric_limits<std::int64_t>::min() ||
          value > std::numeric_limits<std::int64_t>::max()) {
         throw error{error_code::overflow, "ranked signed aggregate overflow"};
      }
      return std::bit_cast<std::uint64_t>(static_cast<std::int64_t>(value));
   }
   if (value < 0) {
      throw error{underflow_is_corruption ? error_code::corruption : error_code::overflow,
                  "ranked unsigned aggregate underflow"};
   }
   if (value > std::numeric_limits<std::uint64_t>::max()) {
      throw error{error_code::overflow, "ranked unsigned aggregate overflow"};
   }
   return static_cast<std::uint64_t>(value);
}

signed_wide decode_scalar(scalar_kind kind, std::uint64_t value) {
   if (kind == scalar_kind::signed_value) {
      return signed_wide{std::bit_cast<std::int64_t>(value)};
   }
   return signed_wide{value};
}

aggregate linear(const layout& descriptor,
                 std::initializer_list<std::pair<const aggregate*, int>> terms,
                 bool underflow_is_corruption = true) {
   auto count = signed_wide{0};
   for (const auto& [value, coefficient] : terms) {
      if (!value || value->sums.size() != descriptor.sum_kinds.size()) {
         throw error{error_code::corruption, "ranked aggregate slot count is invalid"};
      }
      count += signed_wide{value->count} * coefficient;
   }
   if (count < 0) {
      throw error{error_code::corruption, "ranked aggregate count underflow"};
   }
   if (count > std::numeric_limits<std::uint64_t>::max()) {
      throw error{error_code::overflow, "ranked aggregate count overflow"};
   }

   auto result = aggregate{.count = static_cast<std::uint64_t>(count)};
   result.sums.reserve(descriptor.sum_kinds.size());
   for (auto slot = std::size_t{0}; slot < descriptor.sum_kinds.size(); ++slot) {
      auto value = signed_wide{0};
      for (const auto& [term, coefficient] : terms) {
         value += decode_scalar(descriptor.sum_kinds[slot], term->sums[slot]) * coefficient;
      }
      result.sums.push_back(encode_scalar(descriptor.sum_kinds[slot], value, underflow_is_corruption));
   }
   return result;
}

bytes encode_aggregate(const layout& descriptor, const aggregate& value) {
   if (value.sums.size() != descriptor.sum_kinds.size()) {
      throw error{error_code::corruption, "ranked aggregate slot count is invalid"};
   }
   auto out = bytes{};
   out.reserve(sizeof(std::uint64_t) * (1U + value.sums.size()));
   append_u64(out, value.count);
   for (const auto sum : value.sums) {
      append_u64(out, sum);
   }
   return out;
}

aggregate decode_aggregate(const layout& descriptor, const bytes& encoded) {
   const auto expected = sizeof(std::uint64_t) * (1U + descriptor.sum_kinds.size());
   if (encoded.size() != expected) {
      throw error{error_code::corruption, "ranked node value has invalid size"};
   }
   auto offset = std::size_t{0};
   auto result = aggregate{.count = read_u64(encoded, offset)};
   result.sums.reserve(descriptor.sum_kinds.size());
   for (auto slot = std::size_t{0}; slot < descriptor.sum_kinds.size(); ++slot) {
      result.sums.push_back(read_u64(encoded, offset));
   }
   return result;
}

bytes encode_root(const layout& descriptor, const aggregate& total) {
   if (descriptor.schema.empty() || descriptor.schema.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw error{error_code::corruption, "ranked schema descriptor is invalid"};
   }
   auto out = bytes{};
   out.insert(out.end(), root_magic.begin(), root_magic.end());
   out.push_back(static_cast<std::byte>(format_version));
   out.push_back(static_cast<std::byte>(crc64_ecma));
   out.push_back(static_cast<std::byte>(level_count));
   out.push_back(std::byte{0});
   append_u32(out, static_cast<std::uint32_t>(descriptor.schema.size()));
   out.insert(out.end(), descriptor.schema.begin(), descriptor.schema.end());
   const auto aggregate_bytes = encode_aggregate(descriptor, total);
   out.insert(out.end(), aggregate_bytes.begin(), aggregate_bytes.end());
   return out;
}

aggregate decode_root(const layout& descriptor, const bytes& encoded) {
   const auto minimum = root_magic.size() + 4U + sizeof(std::uint32_t);
   if (encoded.size() < minimum || !std::equal(root_magic.begin(), root_magic.end(), encoded.begin())) {
      throw error{error_code::corruption, "ranked root magic is invalid"};
   }
   auto offset = root_magic.size();
   const auto version = std::to_integer<std::uint8_t>(encoded[offset++]);
   const auto hash = std::to_integer<std::uint8_t>(encoded[offset++]);
   const auto levels = std::to_integer<std::uint8_t>(encoded[offset++]);
   ++offset;
   if (version != format_version || hash != crc64_ecma || levels != level_count) {
      throw error{error_code::corruption, "ranked root format configuration is incompatible"};
   }
   const auto schema_size = read_u32(encoded, offset);
   if (schema_size != descriptor.schema.size() || encoded.size() - offset < schema_size ||
       !std::equal(descriptor.schema.begin(), descriptor.schema.end(), encoded.begin() + offset)) {
      throw error{error_code::corruption, "ranked root schema descriptor is incompatible"};
   }
   offset += schema_size;
   auto aggregate_bytes = bytes{encoded.begin() + static_cast<std::ptrdiff_t>(offset), encoded.end()};
   return decode_aggregate(descriptor, aggregate_bytes);
}

bool starts_with(const bytes& value, const bytes& prefix) {
   return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::optional<bytes> prefix_end(bytes prefix) {
   for (auto index = prefix.size(); index > 0U; --index) {
      const auto value = std::to_integer<std::uint8_t>(prefix[index - 1U]);
      if (value == 0xffU) {
         continue;
      }
      prefix[index - 1U] = static_cast<std::byte>(value + 1U);
      prefix.resize(index);
      return prefix;
   }
   return std::nullopt;
}

bytes node_key(const layout& descriptor, std::uint8_t level, const bytes& logical) {
   auto result = descriptor.levels.at(level);
   result.insert(result.end(), logical.begin(), logical.end());
   return result;
}

bytes logical_key(const bytes& prefix, const bytes& key) {
   if (!starts_with(key, prefix)) {
      throw error{error_code::corruption, "ranked node is outside its level prefix"};
   }
   return bytes{key.begin() + static_cast<std::ptrdiff_t>(prefix.size()), key.end()};
}

std::uint8_t height(const bytes& key) {
   using crc_type = boost::crc_optimal<64, 0x42F0E1EBA9EA3693ULL, 0, 0, false, false>;
   auto crc = crc_type{};
   if (!key.empty()) {
      crc.process_bytes(key.data(), key.size());
   }
   auto hash = crc.checksum();
   auto result = std::uint8_t{1};
   while (result < level_count && (hash & 0x0fU) == 0U) {
      ++result;
      hash >>= 4U;
   }
   return result;
}

class planner {
 public:
   planner(const write_access& access, const layout& descriptor)
       : access_{access} {
      (void)descriptor;
   }

   boost::asio::awaitable<std::optional<bytes>> get(const bytes& key) {
      if (const auto found = changes_.find(key); found != changes_.end()) {
         co_return found->second;
      }
      co_return co_await access_.get(key);
   }

   boost::asio::awaitable<std::optional<record>> next(const bytes& prefix, const bytes& after) {
      auto overlay = overlay_next(prefix, after);
      auto cursor = after;
      auto stored = std::optional<record>{};
      while (true) {
         stored = co_await access_.next(prefix, cursor);
         if (!stored.has_value()) {
            break;
         }
         if (!changes_.contains(stored->key)) {
            break;
         }
         cursor = stored->key;
      }
      if (!overlay.has_value()) {
         co_return stored;
      }
      if (!stored.has_value() || overlay->key < stored->key) {
         co_return overlay;
      }
      co_return stored;
   }

   void put(bytes key, bytes value) {
      changes_[std::move(key)] = std::move(value);
   }

   void erase(bytes key) {
      changes_[std::move(key)] = std::nullopt;
   }

   mutation_plan finish() && {
      auto result = mutation_plan{};
      for (auto& [key, value] : changes_) {
         if (value.has_value()) {
            result.puts.emplace_back(std::move(key), std::move(*value));
         } else {
            result.erases.push_back(std::move(key));
         }
      }
      return result;
   }

 private:
   std::optional<record> overlay_next(const bytes& prefix, const bytes& after) const {
      auto found = changes_.upper_bound(after);
      for (; found != changes_.end() && starts_with(found->first, prefix); ++found) {
         if (found->second.has_value()) {
            return record{.key = found->first, .value = *found->second};
         }
      }
      return std::nullopt;
   }

   const write_access& access_;
   std::map<bytes, std::optional<bytes>> changes_;
};

struct search_result {
   aggregate prefix;
   std::array<bytes, level_count> predecessors;
   std::array<aggregate, level_count> predecessor_prefixes;
   std::array<std::optional<bytes>, level_count> next_keys;
};

template <typename Reader>
boost::asio::awaitable<search_result>
search(Reader& reader, const layout& descriptor, const bytes& target) {
   auto result = search_result{.prefix = zero(descriptor)};
   auto current = bytes{};
   for (auto level = level_count; level > 0U; --level) {
      const auto index = static_cast<std::uint8_t>(level - 1U);
      while (true) {
         const auto current_key = node_key(descriptor, index, current);
         const auto current_value = co_await reader.get(current_key);
         if (!current_value.has_value()) {
            throw error{error_code::corruption, "ranked predecessor node is missing"};
         }
         const auto next_record = co_await reader.next(descriptor.levels[index], current_key);
         if (!next_record.has_value()) {
            result.next_keys[index] = std::nullopt;
            break;
         }
         const auto next = logical_key(descriptor.levels[index], next_record->key);
         result.next_keys[index] = next;
         if (!(next < target)) {
            break;
         }
         const auto span = decode_aggregate(descriptor, *current_value);
         result.prefix = linear(descriptor, {{&result.prefix, 1}, {&span, 1}}, false);
         current = next;
      }
      result.predecessors[index] = current;
      result.predecessor_prefixes[index] = result.prefix;
   }
   co_return result;
}

class direct_reader {
 public:
   explicit direct_reader(const read_access& value) : access{value} {}

   boost::asio::awaitable<std::optional<bytes>> get(const bytes& key) {
      co_return co_await access.get(key);
   }

   boost::asio::awaitable<std::optional<record>> next(const bytes& prefix, const bytes& after) {
      co_return co_await access.next(prefix, after);
   }

   const read_access& access;
};

boost::asio::awaitable<std::optional<aggregate>>
load_total(const read_access& access, const layout& descriptor) {
   const auto root = co_await access.get(descriptor.root);
   if (root.has_value()) {
      co_return decode_root(descriptor, *root);
   }
   if (co_await access.has_any(descriptor.object_prefix)) {
      throw error{error_code::rebuild_required,
                  "ranked aggregate state is missing for a populated object type"};
   }
   co_return std::nullopt;
}

boost::asio::awaitable<aggregate>
prefix_aggregate(const read_access& access, const layout& descriptor, const bytes& boundary) {
   auto reader = direct_reader{access};
   co_return (co_await search(reader, descriptor, boundary)).prefix;
}

boost::asio::awaitable<aggregate>
ensure_root(planner& state, const write_access& access, const layout& descriptor) {
   const auto encoded = co_await state.get(descriptor.root);
   if (encoded.has_value()) {
      co_return decode_root(descriptor, *encoded);
   }
   if (co_await access.has_any(descriptor.object_prefix)) {
      throw error{error_code::rebuild_required,
                  "ranked aggregate state is missing for a populated object type"};
   }
   auto total = zero(descriptor);
   state.put(descriptor.root, encode_root(descriptor, total));
   for (auto level = std::uint8_t{0}; level < level_count; ++level) {
      state.put(node_key(descriptor, level, {}), encode_aggregate(descriptor, total));
   }
   co_return total;
}

boost::asio::awaitable<void>
replace_contribution(planner& state, const layout& descriptor,
                     const entry& before, const entry& after, aggregate& total) {
   const auto path = co_await search(state, descriptor, before.key);
   if (!path.next_keys[0].has_value() || *path.next_keys[0] != before.key) {
      throw error{error_code::corruption, "ranked entry is missing during replacement"};
   }
   total = linear(descriptor, {{&total, 1}, {&before.contribution, -1}, {&after.contribution, 1}});
   for (auto level = std::uint8_t{0}; level < level_count; ++level) {
      const auto key = node_key(descriptor, level, path.predecessors[level]);
      const auto encoded = co_await state.get(key);
      if (!encoded.has_value()) {
         throw error{error_code::corruption, "ranked replacement predecessor is missing"};
      }
      auto span = decode_aggregate(descriptor, *encoded);
      span = linear(descriptor, {{&span, 1}, {&before.contribution, -1}, {&after.contribution, 1}});
      state.put(key, encode_aggregate(descriptor, span));
   }
   co_return;
}

boost::asio::awaitable<void>
remove_entry(planner& state, const layout& descriptor, const entry& removed, aggregate& total) {
   const auto path = co_await search(state, descriptor, removed.key);
   if (!path.next_keys[0].has_value() || *path.next_keys[0] != removed.key) {
      throw error{error_code::corruption, "ranked entry is missing during erase"};
   }
   total = linear(descriptor, {{&total, 1}, {&removed.contribution, -1}});
   const auto entry_height = height(removed.key);
   for (auto level = std::uint8_t{0}; level < level_count; ++level) {
      const auto predecessor_key = node_key(descriptor, level, path.predecessors[level]);
      const auto predecessor_value = co_await state.get(predecessor_key);
      if (!predecessor_value.has_value()) {
         throw error{error_code::corruption, "ranked erase predecessor is missing"};
      }
      auto predecessor_span = decode_aggregate(descriptor, *predecessor_value);
      if (level < entry_height) {
         const auto removed_key = node_key(descriptor, level, removed.key);
         const auto removed_value = co_await state.get(removed_key);
         if (!removed_value.has_value()) {
            throw error{error_code::corruption, "ranked promoted node is missing during erase"};
         }
         const auto removed_span = decode_aggregate(descriptor, *removed_value);
         predecessor_span = linear(descriptor,
                                   {{&predecessor_span, 1}, {&removed_span, 1},
                                    {&removed.contribution, -1}});
         state.erase(removed_key);
      } else {
         predecessor_span = linear(descriptor, {{&predecessor_span, 1}, {&removed.contribution, -1}});
      }
      state.put(predecessor_key, encode_aggregate(descriptor, predecessor_span));
   }
   co_return;
}

boost::asio::awaitable<void>
insert_entry(planner& state, const layout& descriptor, const entry& inserted, aggregate& total) {
   const auto path = co_await search(state, descriptor, inserted.key);
   if (path.next_keys[0].has_value() && *path.next_keys[0] == inserted.key) {
      throw error{error_code::corruption, "ranked entry already exists during insert"};
   }
   total = linear(descriptor, {{&total, 1}, {&inserted.contribution, 1}});
   const auto entry_height = height(inserted.key);
   for (auto level = std::uint8_t{0}; level < level_count; ++level) {
      const auto predecessor_key = node_key(descriptor, level, path.predecessors[level]);
      const auto predecessor_value = co_await state.get(predecessor_key);
      if (!predecessor_value.has_value()) {
         throw error{error_code::corruption, "ranked insert predecessor is missing"};
      }
      auto predecessor_span = decode_aggregate(descriptor, *predecessor_value);
      if (level < entry_height) {
         const auto gap = linear(descriptor, {{&path.prefix, 1}, {&path.predecessor_prefixes[level], -1}}, false);
         const auto left = linear(descriptor, {{&gap, 1}, {&inserted.contribution, 1}});
         const auto right = linear(descriptor, {{&predecessor_span, 1}, {&gap, -1}});
         state.put(predecessor_key, encode_aggregate(descriptor, left));
         state.put(node_key(descriptor, level, inserted.key), encode_aggregate(descriptor, right));
      } else {
         predecessor_span = linear(descriptor, {{&predecessor_span, 1}, {&inserted.contribution, 1}});
         state.put(predecessor_key, encode_aggregate(descriptor, predecessor_span));
      }
   }
   co_return;
}

} // namespace

bounds bounds_from_source_range(const layout& descriptor, const bytes& begin,
                                const std::optional<bytes>& end) {
   const auto source_end = prefix_end(descriptor.source_prefix);
   auto result = bounds{};
   if (starts_with(begin, descriptor.source_prefix)) {
      result.lower = bytes{
         begin.begin() + static_cast<std::ptrdiff_t>(descriptor.source_prefix.size()), begin.end()};
   } else if (source_end.has_value() && begin == *source_end) {
      result.lower_at_end = true;
   } else {
      throw error{error_code::corruption, "ranked query begin is outside its source index"};
   }

   if (end.has_value()) {
      if (starts_with(*end, descriptor.source_prefix)) {
         result.upper = bytes{
            end->begin() + static_cast<std::ptrdiff_t>(descriptor.source_prefix.size()), end->end()};
      } else if (!source_end.has_value() || *end != *source_end) {
         throw error{error_code::corruption, "ranked query end is outside its source index"};
      }
   }
   return result;
}

bytes source_key(const layout& descriptor, const bytes& logical) {
   auto result = descriptor.source_prefix;
   result.insert(result.end(), logical.begin(), logical.end());
   return result;
}

boost::asio::awaitable<void> lock_root(const write_access& access, const layout& descriptor) {
   (void)co_await access.lock(descriptor.root);
}

boost::asio::awaitable<aggregate> query(const read_access& access, const layout& descriptor,
                                        const bounds& range) {
   const auto total = co_await load_total(access, descriptor);
   if (!total.has_value() || range.lower_at_end ||
       (range.upper.has_value() && *range.upper < range.lower)) {
      co_return zero(descriptor);
   }
   const auto lower = range.lower.empty() ? zero(descriptor)
                                           : co_await prefix_aggregate(access, descriptor, range.lower);
   const auto upper = !range.upper.has_value() ? *total
                                                : co_await prefix_aggregate(access, descriptor, *range.upper);
   co_return linear(descriptor, {{&upper, 1}, {&lower, -1}}, false);
}

boost::asio::awaitable<rank_result>
query_ranks(const read_access& access, const layout& descriptor, const bounds& range) {
   const auto total = co_await load_total(access, descriptor);
   if (!total.has_value()) {
      co_return rank_result{};
   }
   if (range.lower_at_end) {
      co_return rank_result{.lower = total->count, .upper = total->count, .size = total->count};
   }
   const auto lower = range.lower.empty() ? std::uint64_t{0}
      : (co_await prefix_aggregate(access, descriptor, range.lower)).count;
   if (range.upper.has_value() && *range.upper < range.lower) {
      co_return rank_result{.lower = lower, .upper = lower, .size = total->count};
   }
   const auto upper = !range.upper.has_value() ? total->count
      : (co_await prefix_aggregate(access, descriptor, *range.upper)).count;
   co_return rank_result{.lower = lower, .upper = upper, .size = total->count};
}

boost::asio::awaitable<std::optional<bytes>>
nth_key(const read_access& access, const layout& descriptor, std::uint64_t position) {
   const auto total = co_await load_total(access, descriptor);
   if (!total.has_value() || position >= total->count) {
      co_return std::nullopt;
   }

   auto current = bytes{};
   auto traversed = std::uint64_t{0};
   for (auto level = level_count; level > 0U; --level) {
      const auto index = static_cast<std::uint8_t>(level - 1U);
      while (true) {
         const auto current_key = node_key(descriptor, index, current);
         const auto current_value = co_await access.get(current_key);
         if (!current_value.has_value()) {
            throw error{error_code::corruption, "ranked nth predecessor is missing"};
         }
         const auto next_record = co_await access.next(descriptor.levels[index], current_key);
         if (!next_record.has_value()) {
            break;
         }
         const auto span = decode_aggregate(descriptor, *current_value);
         if (span.count > std::numeric_limits<std::uint64_t>::max() - traversed) {
            throw error{error_code::corruption, "ranked nth span count overflows"};
         }
         if (traversed + span.count > position) {
            if (index == 0U) {
               if (span.count != 1U) {
                  throw error{error_code::corruption, "ranked level zero span is not one"};
               }
               co_return logical_key(descriptor.levels[index], next_record->key);
            }
            break;
         }
         traversed += span.count;
         current = logical_key(descriptor.levels[index], next_record->key);
      }
   }
   throw error{error_code::corruption, "ranked nth traversal did not find an entry"};
}

boost::asio::awaitable<mutation_plan>
plan_change(const write_access& access, const layout& descriptor,
            std::optional<entry> before, std::optional<entry> after) {
   if (!before.has_value() && !after.has_value()) {
      co_return mutation_plan{};
   }
   auto state = planner{access, descriptor};
   auto total = co_await ensure_root(state, access, descriptor);
   if (before.has_value() && after.has_value() && before->key == after->key) {
      co_await replace_contribution(state, descriptor, *before, *after, total);
   } else {
      if (before.has_value()) {
         co_await remove_entry(state, descriptor, *before, total);
      }
      if (after.has_value()) {
         co_await insert_entry(state, descriptor, *after, total);
      }
   }
   state.put(descriptor.root, encode_root(descriptor, total));
   co_return std::move(state).finish();
}

boost::asio::awaitable<void> apply(const write_access& access, mutation_plan plan) {
   for (auto& key : plan.erases) {
      co_await access.erase(std::move(key));
   }
   for (auto& [key, value] : plan.puts) {
      co_await access.put(std::move(key), std::move(value));
   }
}

} // namespace forge::db::object::detail::ranked_index

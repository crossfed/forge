#pragma once

#include "record_key.hxx"

namespace forge::db::object::detail::ordered_key {

using forge::db::object::detail::record_key::append_be32;
using forge::db::object::detail::record_key::append_be64;
using forge::db::object::detail::record_key::append_byte;
using forge::db::object::detail::record_key::entry_kind;

inline void append_framed(std::vector<std::byte>& out, const forge::db::object::sort_key_bytes& payload,
                          forge::db::object::sort_direction direction) {
   auto framed = std::vector<std::byte>{};
   framed.reserve(payload.size() + 2U);
   for (const auto byte : payload) {
      if (byte == std::byte{0U}) {
         framed.push_back(std::byte{0U});
         framed.push_back(std::byte{0xffU});
      } else {
         framed.push_back(byte);
      }
   }
   framed.push_back(std::byte{0U});
   framed.push_back(std::byte{0U});

   if (direction == forge::db::object::sort_direction::descending) {
      for (auto& byte : framed) {
         byte = ~byte;
      }
   }
   out.insert(out.end(), framed.begin(), framed.end());
}

template <forge::db::object::sortable_key Key>
[[nodiscard]] forge::db::object::sort_key_bytes encode_sort_key(const Key& value) {
   try {
      return forge::db::object::sort_key<std::remove_cvref_t<Key>>{}(value);
   } catch (const forge::db::object::exceptions::invalid_index_key&) {
      throw;
   } catch (...) {
      FORGE_THROW_EXCEPTION(forge::db::object::exceptions::invalid_index_key,
                            "db object index key codec rejected a value");
   }
}

template <forge::db::object::key_extractor Extractor, typename Value>
void append_extracted(std::vector<std::byte>& out, const Value& value) {
   using key_type = typename Extractor::value_type;
   const auto& extracted = Extractor::get(value);
   append_framed(out, encode_sort_key<key_type>(extracted), Extractor::direction);
}

template <forge::db::object::key_extractor Extractor, typename Query>
void append_query(std::vector<std::byte>& out, const Query& value) {
   using key_type = typename Extractor::value_type;
   static_assert(std::constructible_from<key_type, const Query&>,
                 "db object query value is not constructible as the declared index key type");
   if constexpr (std::same_as<key_type, std::remove_cvref_t<Query>>) {
      append_framed(out, encode_sort_key<key_type>(value), Extractor::direction);
   } else {
      const auto normalized = key_type(value);
      append_framed(out, encode_sort_key<key_type>(normalized), Extractor::direction);
   }
}

template <typename KeySpec> struct key_encoder;

template <forge::db::object::key_extractor Extractor> struct key_encoder<Extractor> {
   static constexpr auto size = std::size_t{1U};

   template <typename Tuple> [[nodiscard]] static consteval bool accepts_prefix() {
      if constexpr (std::tuple_size_v<std::remove_cvref_t<Tuple>> != 1U) {
         return false;
      } else {
         using query_type = decltype(std::get<0U>(std::declval<const Tuple&>()));
         return std::constructible_from<typename Extractor::value_type, query_type>;
      }
   }

   template <typename Value> static void append_object(std::vector<std::byte>& out, const Value& value) {
      append_extracted<Extractor>(out, value);
   }

   template <typename Tuple> static void append_prefix(std::vector<std::byte>& out, const Tuple& values) {
      static_assert(std::tuple_size_v<std::remove_cvref_t<Tuple>> == 1U,
                    "db object scalar index query requires exactly one key value");
      append_query<Extractor>(out, std::get<0U>(values));
   }
};

template <forge::db::object::key_extractor... Extractors>
struct key_encoder<forge::db::object::composite_key<Extractors...>> {
   using extractor_tuple = std::tuple<Extractors...>;
   static constexpr auto size = sizeof...(Extractors);

   template <typename Tuple, std::size_t... Indexes>
   [[nodiscard]] static consteval bool accepts_prefix_impl(std::index_sequence<Indexes...>) {
      return (std::constructible_from<typename std::tuple_element_t<Indexes, extractor_tuple>::value_type,
                                      decltype(std::get<Indexes>(std::declval<const Tuple&>()))> &&
              ...);
   }

   template <typename Tuple> [[nodiscard]] static consteval bool accepts_prefix() {
      constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
      if constexpr (count == 0U || count > size) {
         return false;
      } else {
         return accepts_prefix_impl<Tuple>(std::make_index_sequence<count>{});
      }
   }

   template <typename Value> static void append_object(std::vector<std::byte>& out, const Value& value) {
      (append_extracted<Extractors>(out, value), ...);
   }

   template <typename Tuple, std::size_t... Indexes>
   static void append_prefix_impl(std::vector<std::byte>& out, const Tuple& values, std::index_sequence<Indexes...>) {
      (append_query<std::tuple_element_t<Indexes, extractor_tuple>>(out, std::get<Indexes>(values)), ...);
   }

   template <typename Tuple> static void append_prefix(std::vector<std::byte>& out, const Tuple& values) {
      constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
      static_assert(count > 0U, "db object composite query requires at least the first key component");
      static_assert(count <= size, "db object composite query prefix is longer than the declared index key");
      append_prefix_impl(out, values, std::make_index_sequence<count>{});
   }
};

inline void append_record_prefix(std::vector<std::byte>& out, entry_kind kind, forge::db::ids::object_id type) {
   forge::db::object::detail::record_key::append_application_prefix(out, kind, type);
}

inline void append_index_prefix(std::vector<std::byte>& out, entry_kind kind, forge::db::ids::object_id type,
                                std::uint32_t ordinal) {
   append_record_prefix(out, kind, type);
   append_be32(out, ordinal);
}

template <typename Object>
[[nodiscard]] forge::db::core::record_key object_record_key(forge::db::object::id_t_of<Object> id) {
   static_assert(forge::db::object::object_model<Object>);
   return forge::db::object::detail::record_key::object(id.as_object_id());
}

template <typename Object, typename Tag>
[[nodiscard]] forge::db::core::record_key index_entry_key(const typename Object::value_type& value) {
   static_assert(forge::db::object::object_model<Object>);
   using index = forge::db::object::index_by_tag<Object, Tag>;
   static_assert(forge::db::object::secondary_index<index>,
                 "db object index_entry_key is only valid for ordered indexes");

   auto bytes = std::vector<std::byte>{};
   constexpr auto kind = index::kind == forge::db::object::index_kind::ordered_unique
                             ? entry_kind::ordered_unique_index
                             : entry_kind::ordered_non_unique_index;
   append_index_prefix(bytes, kind, forge::db::object::object_id_of<Object>::value,
                       forge::db::object::index_id_by_tag<Object, Tag>);
   key_encoder<typename index::key_spec>::append_object(bytes, value);
   if constexpr (index::kind == forge::db::object::index_kind::ordered_non_unique) {
      append_be64(bytes, value.id.instance);
   }
   return forge::db::core::record_key{std::move(bytes)};
}

template <typename Object, typename Tag>
[[nodiscard]] std::vector<std::byte> index_record_prefix() {
   static_assert(forge::db::object::object_model<Object>);
   using index = forge::db::object::index_by_tag<Object, Tag>;
   auto bytes = std::vector<std::byte>{};
   if constexpr (forge::db::object::primary_index<index>) {
      bytes = forge::db::object::detail::record_key::object_prefix(
         forge::db::object::object_id_of<Object>::value);
   } else {
      constexpr auto kind = index::kind == forge::db::object::index_kind::ordered_unique
                                ? entry_kind::ordered_unique_index
                                : entry_kind::ordered_non_unique_index;
      append_index_prefix(bytes, kind, forge::db::object::object_id_of<Object>::value,
                          forge::db::object::index_id_by_tag<Object, Tag>);
   }
   return bytes;
}

template <typename Object, typename Tag>
[[nodiscard]] std::vector<std::byte> logical_key(const typename Object::value_type& value) {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   auto bytes = std::vector<std::byte>{};
   if constexpr (forge::db::object::primary_index<index>) {
      append_be64(bytes, value.id.instance);
   } else {
      key_encoder<typename index::key_spec>::append_object(bytes, value);
      if constexpr (index::kind == forge::db::object::index_kind::ordered_non_unique) {
         append_be64(bytes, value.id.instance);
      }
   }
   return bytes;
}

inline forge::db::core::record_range prefix_range(std::vector<std::byte> prefix);

template <typename Object, typename Tag>
[[nodiscard]] forge::db::core::record_range range_for_value(const typename Object::value_type& value) {
   auto bytes = index_record_prefix<Object, Tag>();
   const auto logical = logical_key<Object, Tag>(value);
   bytes.insert(bytes.end(), logical.begin(), logical.end());
   return prefix_range(std::move(bytes));
}

inline forge::db::core::record_range prefix_range(std::vector<std::byte> prefix) {
   auto scan_prefix = prefix;
   auto end = prefix;
   for (auto index = end.size(); index > 0U; --index) {
      const auto value = static_cast<unsigned>(end[index - 1U]);
      if (value != 0xffU) {
         end[index - 1U] = static_cast<std::byte>(value + 1U);
         end.resize(index);
         return forge::db::core::record_range{.begin = forge::db::core::record_key{std::move(prefix)},
                                              .end = forge::db::core::record_key{std::move(end)},
                                              .prefix = forge::db::core::record_key{std::move(scan_prefix)},
                                              .has_end = true};
      }
   }
   return forge::db::core::record_range{.begin = forge::db::core::record_key{std::move(prefix)},
                                        .end = forge::db::core::record_key{},
                                        .prefix = forge::db::core::record_key{std::move(scan_prefix)},
                                        .has_end = false};
}

template <typename Object, typename Tag, typename Tuple>
[[nodiscard]] forge::db::core::record_range range_from_prefix(const Tuple& values) {
   static_assert(forge::db::object::object_model<Object>);
   using index = forge::db::object::index_by_tag<Object, Tag>;
   auto bytes = index_record_prefix<Object, Tag>();
   if constexpr (forge::db::object::primary_index<index>) {
      static_assert(std::tuple_size_v<std::remove_cvref_t<Tuple>> == 1U,
                    "db object primary query requires one typed id");
      using id_type = forge::db::object::id_t_of<Object>;
      static_assert(std::constructible_from<id_type, decltype(std::get<0U>(values))>,
                    "db object primary query requires its typed id");
      const auto id = id_type(std::get<0U>(values));
      append_be64(bytes, id.instance);
   } else {
      key_encoder<typename index::key_spec>::append_prefix(bytes, values);
   }
   return prefix_range(std::move(bytes));
}

template <typename Object, typename Tag> [[nodiscard]] forge::db::core::record_range range_for_index() {
   static_assert(forge::db::object::object_model<Object>);
   return prefix_range(index_record_prefix<Object, Tag>());
}

template <typename Object, typename Tag, typename Access, typename MatchId>
boost::asio::awaitable<bool>
ranked_entry_exists(Access source, const typename Object::value_type& value, MatchId match_id) {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   static_assert(forge::db::object::ranked_index<index>);

   if (!(co_await source.get(object_record_key<Object>(value.id))).has_value()) {
      co_return false;
   }
   if constexpr (forge::db::object::primary_index<index>) {
      co_return true;
   } else {
      const auto encoded = co_await source.get(index_entry_key<Object, Tag>(value));
      if (!encoded.has_value()) {
         co_return false;
      }
      if constexpr (index::kind == forge::db::object::index_kind::ordered_unique) {
         co_return std::invoke(match_id, *encoded, value.id);
      }
      co_return true;
   }
}

template <typename Object, typename Tag, typename Lower, typename Upper>
[[nodiscard]] forge::db::core::record_range range_between(const Lower& lower, const Upper& upper) {
   auto result = range_for_index<Object, Tag>();
   result.begin = range_from_prefix<Object, Tag>(lower).begin;
   result.end = range_from_prefix<Object, Tag>(upper).begin;
   result.has_end = true;
   return result;
}

template <typename Object, typename Tag, typename Tuple> [[nodiscard]] consteval bool accepts_prefix_query() {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   if constexpr (forge::db::object::primary_index<index>) {
      if constexpr (std::tuple_size_v<std::remove_cvref_t<Tuple>> != 1U) {
         return false;
      } else {
         return std::constructible_from<forge::db::object::id_t_of<Object>,
                                        decltype(std::get<0U>(std::declval<const Tuple&>()))>;
      }
   } else {
      return key_encoder<typename index::key_spec>::template accepts_prefix<Tuple>();
   }
}

template <typename Object, typename Tag, typename Tuple> [[nodiscard]] consteval bool accepts_full_query() {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   if constexpr (forge::db::object::primary_index<index>) {
      return accepts_prefix_query<Object, Tag, Tuple>();
   } else {
      return key_encoder<typename index::key_spec>::size == std::tuple_size_v<std::remove_cvref_t<Tuple>> &&
             key_encoder<typename index::key_spec>::template accepts_prefix<Tuple>();
   }
}

template <typename Sum>
[[nodiscard]] consteval forge::db::object::detail::ranked_index::scalar_kind ranked_sum_kind() {
   if constexpr (std::same_as<typename Sum::accumulator_type, std::int64_t>) {
      return forge::db::object::detail::ranked_index::scalar_kind::signed_value;
   } else {
      return forge::db::object::detail::ranked_index::scalar_kind::unsigned_value;
   }
}

template <typename Tuple, std::size_t... Indexes>
void append_ranked_sum_kinds(std::vector<forge::db::object::detail::ranked_index::scalar_kind>& out,
                             std::index_sequence<Indexes...>) {
   (out.push_back(ranked_sum_kind<std::tuple_element_t<Indexes, Tuple>>()), ...);
}

template <typename Accumulator, std::integral Value>
   requires(!std::same_as<Value, bool>)
[[nodiscard]] std::uint64_t ranked_contribution_word(Value value) {
   if constexpr (std::same_as<Accumulator, std::int64_t>) {
      if constexpr (std::unsigned_integral<Value>) {
         if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw forge::db::object::detail::ranked_index::error{
               forge::db::object::detail::ranked_index::error_code::overflow,
               "ranked sum projection exceeds signed accumulator"};
         }
      }
      return std::bit_cast<std::uint64_t>(static_cast<std::int64_t>(value));
   } else {
      if constexpr (std::signed_integral<Value>) {
         if (value < 0) {
            throw forge::db::object::detail::ranked_index::error{
               forge::db::object::detail::ranked_index::error_code::overflow,
               "ranked sum projection is negative for unsigned accumulator"};
         }
      }
      return static_cast<std::uint64_t>(value);
   }
}

template <typename Sums, typename Value, std::size_t... Indexes>
void append_ranked_contributions(forge::db::object::detail::ranked_index::aggregate& out,
                                 const Value& value, std::index_sequence<Indexes...>) {
   (out.sums.push_back(ranked_contribution_word<
       typename std::tuple_element_t<Indexes, Sums>::accumulator_type>(
       std::tuple_element_t<Indexes, Sums>::projection::get(value))), ...);
}

template <typename Index, typename Value>
[[nodiscard]] forge::db::object::detail::ranked_index::aggregate
ranked_contribution(const Value& value) {
   using sums = typename Index::sums_type;
   auto result = forge::db::object::detail::ranked_index::aggregate{.count = 1U};
   result.sums.reserve(std::tuple_size_v<sums>);
   append_ranked_contributions<sums>(result, value,
                                     std::make_index_sequence<std::tuple_size_v<sums>>{});
   return result;
}

template <typename Object, typename Tag>
[[nodiscard]] forge::db::object::detail::ranked_index::layout ranked_layout() {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   static_assert(forge::db::object::ranked_index<index>);
   auto result = forge::db::object::detail::ranked_index::layout{};
   const auto type = forge::db::object::object_id_of<Object>::value;
   const auto ordinal = forge::db::object::index_id_by_tag<Object, Tag>;
   result.root = forge::db::object::detail::record_key::ranked_root(type, ordinal).bytes();
   result.levels.reserve(forge::db::object::detail::ranked_index::level_count);
   for (auto level = std::uint8_t{0}; level < forge::db::object::detail::ranked_index::level_count; ++level) {
      result.levels.push_back(forge::db::object::detail::record_key::ranked_level_prefix(type, ordinal, level));
   }
   result.source_prefix = index_record_prefix<Object, Tag>();
   result.object_prefix = forge::db::object::detail::record_key::object_prefix(type);
   using sums = typename index::sums_type;
   result.sum_kinds.reserve(std::tuple_size_v<sums>);
   append_ranked_sum_kinds<sums>(result.sum_kinds,
                                 std::make_index_sequence<std::tuple_size_v<sums>>{});

   // Canonical persisted descriptor. Arbitrary C++ extractors cannot be named
   // portably, so their semantic identity is owned by the explicit schema version.
   constexpr auto key_layout_version = std::uint8_t{1};
   result.schema.reserve(20U + result.source_prefix.size() + result.sum_kinds.size());
   forge::db::object::detail::record_key::append_byte(result.schema, key_layout_version);
   forge::db::object::detail::record_key::append_byte(
      result.schema, static_cast<std::uint8_t>(index::kind));
   forge::db::object::detail::record_key::append_byte(result.schema, type.space);
   forge::db::object::detail::record_key::append_be16(result.schema, type.type);
   forge::db::object::detail::record_key::append_be32(result.schema, ordinal);
   forge::db::object::detail::record_key::append_be64(
      result.schema, index::schema_type::version);
   forge::db::object::detail::record_key::append_be32(
      result.schema, static_cast<std::uint32_t>(result.sum_kinds.size()));
   for (const auto kind : result.sum_kinds) {
      forge::db::object::detail::record_key::append_byte(
         result.schema, static_cast<std::uint8_t>(kind));
   }
   return result;
}

template <typename Object, typename Tag>
[[nodiscard]] forge::db::object::detail::ranked_index::bounds
ranked_bounds(const forge::db::object::detail::ranked_index::layout& descriptor,
              const forge::db::core::record_range& range) {
   auto end = std::optional<std::vector<std::byte>>{};
   if (range.has_end) {
      end = range.end.bytes();
   }
   return forge::db::object::detail::ranked_index::bounds_from_source_range(
      descriptor, range.begin.bytes(), end);
}

} // namespace forge::db::object::detail::ordered_key

#pragma once

namespace forge::db::object::detail::ordered_key {

enum class entry_kind : std::uint8_t {
   object_record = 0x10,
   ordered_unique_index = 0x20,
   ordered_non_unique_index = 0x21,
};

inline void append_byte(std::vector<std::byte>& out, std::uint8_t value) {
   out.push_back(static_cast<std::byte>(value));
}

inline void append_be16(std::vector<std::byte>& out, std::uint16_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

inline void append_be32(std::vector<std::byte>& out, std::uint32_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

inline void append_be64(std::vector<std::byte>& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      append_byte(out, static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

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

inline void append_record_prefix(std::vector<std::byte>& out, entry_kind kind, forge::ids::object_id type) {
   append_byte(out, static_cast<std::uint8_t>(kind));
   append_byte(out, type.space);
   append_be16(out, type.type);
}

inline void append_index_prefix(std::vector<std::byte>& out, entry_kind kind, forge::ids::object_id type,
                                std::uint32_t ordinal) {
   append_record_prefix(out, kind, type);
   append_be32(out, ordinal);
}

template <typename Object>
[[nodiscard]] forge::db::core::record_key object_record_key(forge::db::object::id_t_of<Object> id) {
   static_assert(forge::db::object::object_model<Object>);
   auto bytes = std::vector<std::byte>{};
   append_record_prefix(bytes, entry_kind::object_record, forge::db::object::object_id_of<Object>::value);
   append_be64(bytes, id.instance);
   return forge::db::core::record_key{std::move(bytes)};
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
   static_assert(forge::db::object::secondary_index<index>,
                 "db object range_from_prefix is only valid for ordered indexes");

   auto bytes = std::vector<std::byte>{};
   constexpr auto kind = index::kind == forge::db::object::index_kind::ordered_unique
                             ? entry_kind::ordered_unique_index
                             : entry_kind::ordered_non_unique_index;
   append_index_prefix(bytes, kind, forge::db::object::object_id_of<Object>::value,
                       forge::db::object::index_id_by_tag<Object, Tag>);
   key_encoder<typename index::key_spec>::append_prefix(bytes, values);
   return prefix_range(std::move(bytes));
}

template <typename Object, typename Tag> [[nodiscard]] forge::db::core::record_range range_for_index() {
   static_assert(forge::db::object::object_model<Object>);
   using index = forge::db::object::index_by_tag<Object, Tag>;
   static_assert(forge::db::object::secondary_index<index>,
                 "db object range_for_index is only valid for ordered indexes");

   auto bytes = std::vector<std::byte>{};
   constexpr auto kind = index::kind == forge::db::object::index_kind::ordered_unique
                             ? entry_kind::ordered_unique_index
                             : entry_kind::ordered_non_unique_index;
   append_index_prefix(bytes, kind, forge::db::object::object_id_of<Object>::value,
                       forge::db::object::index_id_by_tag<Object, Tag>);
   return prefix_range(std::move(bytes));
}

template <typename Object, typename Tag, typename Tuple> [[nodiscard]] consteval bool accepts_prefix_query() {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   if constexpr (!forge::db::object::secondary_index<index>) {
      return false;
   } else {
      return key_encoder<typename index::key_spec>::template accepts_prefix<Tuple>();
   }
}

template <typename Object, typename Tag, typename Tuple> [[nodiscard]] consteval bool accepts_full_query() {
   using index = forge::db::object::index_by_tag<Object, Tag>;
   if constexpr (!forge::db::object::secondary_index<index>) {
      return false;
   } else {
      return key_encoder<typename index::key_spec>::size == std::tuple_size_v<std::remove_cvref_t<Tuple>> &&
             key_encoder<typename index::key_spec>::template accepts_prefix<Tuple>();
   }
}

} // namespace forge::db::object::detail::ordered_key

module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.db.object.index;

import forge.ids.object_id;
import forge.db.core.record;
import forge.db.object.cursor;
import forge.db.object.exceptions;
import forge.db.object.object;

export namespace forge::db::object {

template <typename T> struct object_page {
   std::vector<T> items;
   std::optional<forge::db::core::cursor> next;
};

struct stream_options {
   std::uint32_t page_size = forge::db::core::default_page_limit;
};

enum class index_kind : std::uint8_t {
   primary_unique = 1,
   ordered_unique = 2,
   ordered_non_unique = 3,
};

enum class sort_direction : std::uint8_t {
   ascending,
   descending,
};

using sort_key_bytes = std::vector<std::byte>;

template <typename T> struct sort_key;

template <typename T>
concept sortable_key = requires(const std::remove_cvref_t<T>& value) {
   { sort_key<std::remove_cvref_t<T>>{}(value) } -> std::same_as<sort_key_bytes>;
};

template <> struct sort_key<bool> {
   [[nodiscard]] sort_key_bytes operator()(bool value) const {
      return {value ? std::byte{1U} : std::byte{0U}};
   }
};

template <std::unsigned_integral T>
   requires(!std::same_as<T, bool>)
struct sort_key<T> {
   [[nodiscard]] sort_key_bytes operator()(T value) const {
      auto out = sort_key_bytes{};
      out.reserve(sizeof(T));
      for (auto index = sizeof(T); index > 0U; --index) {
         const auto shift = static_cast<unsigned>((index - 1U) * 8U);
         out.push_back(static_cast<std::byte>((value >> shift) & static_cast<T>(0xffU)));
      }
      return out;
   }
};

template <std::signed_integral T> struct sort_key<T> {
   [[nodiscard]] sort_key_bytes operator()(T value) const {
      using unsigned_type = std::make_unsigned_t<T>;
      auto encoded = static_cast<unsigned_type>(value);
      encoded ^= unsigned_type{1U} << (std::numeric_limits<unsigned_type>::digits - 1U);
      return sort_key<unsigned_type>{}(encoded);
   }
};

template <typename T>
   requires std::is_enum_v<T>
struct sort_key<T> {
   [[nodiscard]] sort_key_bytes operator()(T value) const {
      return sort_key<std::underlying_type_t<T>>{}(static_cast<std::underlying_type_t<T>>(value));
   }
};

template <> struct sort_key<std::string> {
   [[nodiscard]] sort_key_bytes operator()(const std::string& value) const {
      auto out = sort_key_bytes{};
      out.reserve(value.size());
      for (const auto byte : value) {
         out.push_back(static_cast<std::byte>(byte));
      }
      return out;
   }
};

template <> struct sort_key<std::string_view> {
   [[nodiscard]] sort_key_bytes operator()(std::string_view value) const {
      auto out = sort_key_bytes{};
      out.reserve(value.size());
      for (const auto byte : value) {
         out.push_back(static_cast<std::byte>(byte));
      }
      return out;
   }
};

template <> struct sort_key<forge::ids::object_id> {
   [[nodiscard]] sort_key_bytes operator()(forge::ids::object_id value) const {
      auto out = sort_key_bytes{};
      out.reserve(11U);
      out.push_back(static_cast<std::byte>(value.space));
      const auto type = sort_key<std::uint16_t>{}(value.type);
      out.insert(out.end(), type.begin(), type.end());
      const auto instance = sort_key<std::uint64_t>{}(value.instance);
      out.insert(out.end(), instance.begin(), instance.end());
      return out;
   }
};

template <forge::ids::typed_id_like T> struct sort_key<T> {
   [[nodiscard]] sort_key_bytes operator()(const T& value) const {
      return sort_key<forge::ids::object_id>{}(value.as_object_id());
   }
};

template <typename T>
   requires(!forge::ids::typed_id_like<T> &&
            requires(const T& value) {
               { value.to_uint8_span() } -> std::convertible_to<std::span<const std::uint8_t>>;
            })
struct sort_key<T> {
   [[nodiscard]] sort_key_bytes operator()(const T& value) const {
      const auto bytes = std::span<const std::uint8_t>{value.to_uint8_span()};
      auto out = sort_key_bytes{};
      out.reserve(bytes.size());
      for (const auto byte : bytes) {
         out.push_back(static_cast<std::byte>(byte));
      }
      return out;
   }
};

template <typename T>
   requires(
       !forge::ids::typed_id_like<T> &&
       !requires(const T& value) {
          { value.to_uint8_span() } -> std::convertible_to<std::span<const std::uint8_t>>;
       } &&
       requires(const T& value) {
          value.extract_as_byte_array();
          requires std::same_as<typename std::remove_cvref_t<decltype(value.extract_as_byte_array())>::value_type,
                                std::uint8_t>;
       })
struct sort_key<T> {
   [[nodiscard]] sort_key_bytes operator()(const T& value) const {
      const auto bytes = value.extract_as_byte_array();
      auto out = sort_key_bytes{};
      out.reserve(bytes.size());
      for (const auto byte : bytes) {
         out.push_back(static_cast<std::byte>(byte));
      }
      return out;
   }
};

template <auto Pointer> struct member;

template <typename Owner, typename Value, Value Owner::* Member> struct member<Member> {
   using owner_type = Owner;
   using value_type = std::remove_cvref_t<Value>;
   static constexpr auto pointer = Member;
   static constexpr auto direction = sort_direction::ascending;

   [[nodiscard]] static constexpr decltype(auto) get(const Owner& value) noexcept {
      return value.*Member;
   }
};

template <auto Pointer> struct const_mem_fun {
 private:
   template <typename Owner, typename Value> static std::type_identity<Owner> owner_of(Value (Owner::*)() const);

   template <typename Owner, typename Value>
   static std::type_identity<Owner> owner_of(Value (Owner::*)() const noexcept);

 public:
   using owner_type = typename decltype(owner_of(Pointer))::type;
   using value_type = std::remove_cvref_t<std::invoke_result_t<decltype(Pointer), const owner_type&>>;
   static constexpr auto pointer = Pointer;
   static constexpr auto direction = sort_direction::ascending;

   [[nodiscard]] static constexpr decltype(auto)
   get(const owner_type& value) noexcept(noexcept(std::invoke(Pointer, value))) {
      return std::invoke(Pointer, value);
   }
};

template <auto Pointer> struct global_fun {
 private:
   template <typename Owner, typename Value> static std::type_identity<Owner> owner_of(Value (*)(const Owner&));

   template <typename Owner, typename Value>
   static std::type_identity<Owner> owner_of(Value (*)(const Owner&) noexcept);

 public:
   using owner_type = typename decltype(owner_of(Pointer))::type;
   using value_type = std::remove_cvref_t<std::invoke_result_t<decltype(Pointer), const owner_type&>>;
   static constexpr auto pointer = Pointer;
   static constexpr auto direction = sort_direction::ascending;

   [[nodiscard]] static constexpr decltype(auto)
   get(const owner_type& value) noexcept(noexcept(std::invoke(Pointer, value))) {
      return std::invoke(Pointer, value);
   }
};

template <typename T>
concept key_extractor = requires(const typename T::owner_type& value) {
   typename T::owner_type;
   typename T::value_type;
   { T::direction } -> std::convertible_to<sort_direction>;
   T::get(value);
} && sortable_key<typename T::value_type>;

template <key_extractor Extractor> struct ascending {
   using owner_type = typename Extractor::owner_type;
   using value_type = typename Extractor::value_type;
   static constexpr auto direction = sort_direction::ascending;

   [[nodiscard]] static constexpr decltype(auto)
   get(const owner_type& value) noexcept(noexcept(Extractor::get(value))) {
      return Extractor::get(value);
   }
};

template <key_extractor Extractor> struct descending {
   using owner_type = typename Extractor::owner_type;
   using value_type = typename Extractor::value_type;
   static constexpr auto direction = sort_direction::descending;

   [[nodiscard]] static constexpr decltype(auto)
   get(const owner_type& value) noexcept(noexcept(Extractor::get(value))) {
      return Extractor::get(value);
   }
};

template <key_extractor... Extractors> struct composite_key {
   static_assert(sizeof...(Extractors) > 1U, "forge::db::object::composite_key requires at least two extractors");

 private:
   using first_type = std::tuple_element_t<0U, std::tuple<Extractors...>>;

 public:
   using owner_type = typename first_type::owner_type;
   using extractor_tuple = std::tuple<Extractors...>;
   static constexpr std::size_t size = sizeof...(Extractors);

   static_assert((std::same_as<owner_type, typename Extractors::owner_type> && ...),
                 "forge::db::object composite key extractors must have the same owner type");
};

template <typename T>
concept composite_key_spec = requires {
   typename T::owner_type;
   typename T::extractor_tuple;
   { T::size } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept ordered_key_spec = key_extractor<T> || composite_key_spec<T>;

struct primary_key {
   static constexpr std::size_t size = 1;
};

template <typename Tag> struct primary_unique {
   using tag_type = Tag;
   using key_spec = primary_key;

   static constexpr index_kind kind = index_kind::primary_unique;
};

template <typename Tag, ordered_key_spec Extractor> struct ordered_unique {
   using tag_type = Tag;
   using owner_type = typename Extractor::owner_type;
   using key_spec = Extractor;
   static constexpr index_kind kind = index_kind::ordered_unique;
};

template <typename Tag, ordered_key_spec Extractor> struct ordered_non_unique {
   using tag_type = Tag;
   using owner_type = typename Extractor::owner_type;
   using key_spec = Extractor;
   static constexpr index_kind kind = index_kind::ordered_non_unique;
};

template <typename... Indexes> struct indexed_by {
   using tuple_type = std::tuple<Indexes...>;
   static constexpr std::size_t size = sizeof...(Indexes);
};

template <typename Value, bool Valid> struct object_index_value_traits {};

template <typename Value> struct object_index_value_traits<Value, true> {
   using base_type = typename Value::object_base_type;
   using id_t = typename Value::id_t;
};

template <typename Value, typename Indexes>
struct object_index : object_index_value_traits<Value, object_value<Value>> {
   using value_type = Value;
   using indexes_type = Indexes;
};

template <typename T> struct is_primary_index : std::false_type {};

template <typename Tag> struct is_primary_index<primary_unique<Tag>> : std::true_type {};

template <typename T> inline constexpr bool is_primary_index_v = is_primary_index<T>::value;

template <typename T> struct is_secondary_index : std::false_type {};

template <typename Tag, typename Extractor>
struct is_secondary_index<ordered_unique<Tag, Extractor>> : std::true_type {};

template <typename Tag, typename Extractor>
struct is_secondary_index<ordered_non_unique<Tag, Extractor>> : std::true_type {};

template <typename T> inline constexpr bool is_secondary_index_v = is_secondary_index<T>::value;

template <typename T>
concept index_model = requires {
   typename T::tag_type;
   typename T::key_spec;
   { T::kind } -> std::convertible_to<index_kind>;
};

template <typename T>
concept primary_index = index_model<T> && is_primary_index_v<T>;

template <typename T>
concept secondary_index = index_model<T> && is_secondary_index_v<T>;

} // namespace forge::db::object

namespace forge::db::object::detail {

template <typename T> struct is_indexed_by : std::false_type {};

template <typename... Indexes> struct is_indexed_by<indexed_by<Indexes...>> : std::true_type {};

template <typename T> inline constexpr bool is_indexed_by_v = is_indexed_by<T>::value;

template <typename Indexes> struct primary_count;

template <typename... Indexes> struct primary_count<indexed_by<Indexes...>> {
   static constexpr std::size_t value = (std::size_t{0} + ... + (is_primary_index_v<Indexes> ? 1U : 0U));
};

template <typename Object, typename Indexes> struct indexes_match_object;

template <typename Object, typename... Indexes> struct indexes_match_object<Object, indexed_by<Indexes...>> {
   template <typename Index> static constexpr bool matches_index() {
      if constexpr (is_primary_index_v<Index>) {
         return true;
      } else {
         return std::same_as<typename Index::owner_type, typename Object::value_type>;
      }
   }

   static constexpr bool value = (... && matches_index<Indexes>());
};

template <typename Indexes> struct first_primary_index;

template <typename First, typename... Rest> struct first_primary_index<indexed_by<First, Rest...>> {
   using type =
       std::conditional_t<is_primary_index_v<First>, First, typename first_primary_index<indexed_by<Rest...>>::type>;
};

template <> struct first_primary_index<indexed_by<>> {
   using type = void;
};

template <typename Object> using primary_id_t = typename Object::id_t;

template <typename Indexes> struct unique_tags;

template <> struct unique_tags<indexed_by<>> : std::true_type {};

template <typename First, typename... Rest>
struct unique_tags<indexed_by<First, Rest...>>
    : std::bool_constant<(!std::same_as<typename First::tag_type, typename Rest::tag_type> && ...) &&
                         unique_tags<indexed_by<Rest...>>::value> {};

template <typename Object, bool HasShape> struct valid_object_impl : std::false_type {};

template <typename Object> struct valid_object_impl<Object, true> {
 private:
   static constexpr bool indexed = is_indexed_by_v<typename Object::indexes_type>;
   static constexpr bool one_primary = indexed && primary_count<typename Object::indexes_type>::value == 1;
   static constexpr bool owner_match = indexed && indexes_match_object<Object, typename Object::indexes_type>::value;
   static constexpr bool tags_unique = indexed && unique_tags<typename Object::indexes_type>::value;
   static constexpr bool value_has_base = object_value<typename Object::value_type>;
   static constexpr bool primary_is_typed =
       forge::ids::typed_id_traits<std::remove_cvref_t<primary_id_t<Object>>>::is_typed_id;

 public:
   static constexpr bool value =
       indexed && one_primary && owner_match && tags_unique && value_has_base && primary_is_typed;
};

template <typename Object> struct valid_object : valid_object_impl < Object, requires {
   typename Object::value_type;
   typename Object::id_t;
   typename Object::indexes_type;
}>{};

template <typename Tag, std::size_t Position, typename... Indexes> struct find_index_by_tag_impl;

template <typename Tag, std::size_t Position, typename First, typename... Rest>
struct find_index_by_tag_impl<Tag, Position, First, Rest...> {
   using next = find_index_by_tag_impl<Tag, Position + 1, Rest...>;
   using type = std::conditional_t<std::same_as<Tag, typename First::tag_type>, First, typename next::type>;
   static constexpr std::size_t position = std::same_as<Tag, typename First::tag_type> ? Position : next::position;
   static constexpr bool found = std::same_as<Tag, typename First::tag_type> || next::found;
};

template <typename Tag, std::size_t Position> struct find_index_by_tag_impl<Tag, Position> {
   using type = void;
   static constexpr std::size_t position = Position;
   static constexpr bool found = false;
};

} // namespace forge::db::object::detail

export namespace forge::db::object {

template <typename T>
concept object_model = detail::valid_object<T>::value;

template <typename T>
concept application_object_model = object_model<T> && application_object_value<typename T::value_type>;

template <typename T>
concept system_object_model = object_model<T> && system_object_value<typename T::value_type>;

template <object_model Object> using id_t_of = detail::primary_id_t<Object>;

template <object_model Object> struct object_id_of {
 private:
   using id_t = std::remove_cvref_t<id_t_of<Object>>;

 public:
   static constexpr std::uint8_t space = forge::ids::typed_id_traits<id_t>::space;
   static constexpr std::uint16_t type = forge::ids::typed_id_traits<id_t>::type;
   static constexpr forge::ids::object_id value{.space = space, .type = type, .instance = 0};
};

template <typename Object, typename Tag> struct index_lookup;

template <typename Value, typename... Indexes, typename Tag>
struct index_lookup<object_index<Value, indexed_by<Indexes...>>, Tag> {
 private:
   using impl = detail::find_index_by_tag_impl<Tag, 0, Indexes...>;
   static_assert(impl::found, "forge::db::object index tag is not registered for this object");

 public:
   using type = typename impl::type;
   static constexpr std::size_t position = impl::position;
};

template <object_model Object, typename Tag> using index_by_tag = typename index_lookup<Object, Tag>::type;

template <object_model Object, typename Tag>
inline constexpr std::uint32_t index_id_by_tag = static_cast<std::uint32_t>(index_lookup<Object, Tag>::position);

template <object_model Object, typename Tag> class index_view;

} // namespace forge::db::object

#include "ordered_key.hxx"

namespace forge::db::object::detail {

template <typename T> struct is_tuple_key : std::false_type {};

template <typename... Values> struct is_tuple_key<std::tuple<Values...>> : std::true_type {};

template <typename T> inline constexpr bool is_tuple_key_v = is_tuple_key<std::remove_cvref_t<T>>::value;

} // namespace forge::db::object::detail

export namespace forge::db::object {

template <typename T>
using index_page_query =
    std::function<boost::asio::awaitable<object_page<T>>(forge::db::core::record_range, forge::db::core::page_request)>;

template <typename T> using index_stream_query_factory = std::function<index_page_query<T>()>;

template <typename T> class index_stream {
 public:
   index_stream() = default;

   index_stream(index_page_query<T> query, forge::db::core::record_range range, stream_options options)
       : query_{std::move(query)}, range_{std::move(range)}, page_size_{options.page_size} {}

   boost::asio::awaitable<std::optional<T>> next() {
      forge::db::object::validate_page_request(forge::db::core::page_request{.limit = page_size_});
      if (offset_ < current_.items.size()) {
         co_return current_.items[offset_++];
      }
      if (exhausted_) {
         co_return std::nullopt;
      }

      current_ = co_await query_(range_,
                                 forge::db::core::page_request{.after = std::move(current_.next), .limit = page_size_});
      offset_ = 0;
      if (current_.items.empty()) {
         exhausted_ = !current_.next.has_value();
         co_return std::nullopt;
      }
      if (!current_.next.has_value()) {
         exhausted_ = true;
      }
      co_return current_.items[offset_++];
   }

 private:
   index_page_query<T> query_;
   forge::db::core::record_range range_;
   object_page<T> current_;
   std::size_t offset_ = 0;
   std::uint32_t page_size_ = forge::db::core::default_page_limit;
   bool exhausted_ = false;
};

template <object_model Object, typename Tag> class range_query {
 public:
   using value_type = typename Object::value_type;

   range_query() = default;

   range_query(index_page_query<value_type> page, index_stream_query_factory<value_type> stream_page,
               forge::db::core::record_range range)
       : page_{std::move(page)}, stream_page_{std::move(stream_page)}, range_{std::move(range)} {}

   boost::asio::awaitable<object_page<value_type>> page(forge::db::core::page_request request = {}) {
      co_return co_await page_(range_, std::move(request));
   }

   [[nodiscard]] index_stream<value_type> stream(stream_options options = {}) {
      auto query = stream_page_ ? stream_page_() : page_;
      return index_stream<value_type>{std::move(query), range_, options};
   }

   template <typename Fn> boost::asio::awaitable<void> for_each(stream_options options, Fn&& fn) {
      auto values = stream(options);
      while (auto value = co_await values.next()) {
         co_await std::invoke(fn, *value);
      }
      co_return;
   }

 private:
   index_page_query<value_type> page_;
   index_stream_query_factory<value_type> stream_page_;
   forge::db::core::record_range range_;
};

template <object_model Object, typename Tag> class index_view {
 public:
   using value_type = typename Object::value_type;

   index_view() = default;

   explicit index_view(index_page_query<value_type> page, index_stream_query_factory<value_type> stream_page = {})
       : page_{std::move(page)}, stream_page_{std::move(stream_page)} {}

   boost::asio::awaitable<object_page<value_type>> page(forge::db::core::record_range range,
                                                        forge::db::core::page_request request) {
      co_return co_await page_(std::move(range), std::move(request));
   }

   template <typename... Keys>
      requires(sizeof...(Keys) > 0U && !(sizeof...(Keys) == 1U && (detail::is_tuple_key_v<Keys> && ...)) &&
               detail::ordered_key::accepts_full_query<Object, Tag, std::tuple<const Keys&...>>())
   boost::asio::awaitable<std::optional<value_type>> find(const Keys&... keys) {
      co_return co_await find(std::tie(keys...));
   }

   template <typename... Keys>
      requires(detail::ordered_key::accepts_full_query<Object, Tag, std::tuple<Keys...>>())
   boost::asio::awaitable<std::optional<value_type>> find(const std::tuple<Keys...>& key) {
      auto result = co_await equal_range(key).page(forge::db::core::page_request{.limit = 1});
      if (result.items.empty()) {
         co_return std::nullopt;
      }
      co_return result.items.front();
   }

   template <typename... PrefixValues>
      requires(detail::ordered_key::accepts_prefix_query<Object, Tag, std::tuple<PrefixValues...>>())
   [[nodiscard]] range_query<Object, Tag> equal_range(const std::tuple<PrefixValues...>& prefix) const {
      return range_query<Object, Tag>{page_, stream_page_, detail::ordered_key::range_from_prefix<Object, Tag>(prefix)};
   }

   template <typename... PrefixValues>
      requires(sizeof...(PrefixValues) > 0U &&
               !(sizeof...(PrefixValues) == 1U && (detail::is_tuple_key_v<PrefixValues> && ...)) &&
               detail::ordered_key::accepts_prefix_query<Object, Tag, std::tuple<const PrefixValues&...>>())
   [[nodiscard]] range_query<Object, Tag> equal_range(const PrefixValues&... values) const {
      return equal_range(std::tie(values...));
   }

   template <typename... PrefixValues>
      requires(detail::ordered_key::accepts_prefix_query<Object, Tag, std::tuple<PrefixValues...>>())
   [[nodiscard]] range_query<Object, Tag> lower_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = detail::ordered_key::range_for_index<Object, Tag>();
      range.begin = detail::ordered_key::range_from_prefix<Object, Tag>(prefix).begin;
      return range_query<Object, Tag>{page_, stream_page_, std::move(range)};
   }

   template <typename... PrefixValues>
      requires(sizeof...(PrefixValues) > 0U &&
               !(sizeof...(PrefixValues) == 1U && (detail::is_tuple_key_v<PrefixValues> && ...)) &&
               detail::ordered_key::accepts_prefix_query<Object, Tag, std::tuple<const PrefixValues&...>>())
   [[nodiscard]] range_query<Object, Tag> lower_bound(const PrefixValues&... values) const {
      return lower_bound(std::tie(values...));
   }

   template <typename... PrefixValues>
      requires(detail::ordered_key::accepts_prefix_query<Object, Tag, std::tuple<PrefixValues...>>())
   [[nodiscard]] range_query<Object, Tag> upper_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = detail::ordered_key::range_for_index<Object, Tag>();
      auto exact = detail::ordered_key::range_from_prefix<Object, Tag>(prefix);
      range.begin = exact.has_end ? std::move(exact.end) : std::move(exact.begin);
      return range_query<Object, Tag>{page_, stream_page_, std::move(range)};
   }

   template <typename... PrefixValues>
      requires(sizeof...(PrefixValues) > 0U &&
               !(sizeof...(PrefixValues) == 1U && (detail::is_tuple_key_v<PrefixValues> && ...)) &&
               detail::ordered_key::accepts_prefix_query<Object, Tag, std::tuple<const PrefixValues&...>>())
   [[nodiscard]] range_query<Object, Tag> upper_bound(const PrefixValues&... values) const {
      return upper_bound(std::tie(values...));
   }

 private:
   index_page_query<value_type> page_;
   index_stream_query_factory<value_type> stream_page_;
};

} // namespace forge::db::object

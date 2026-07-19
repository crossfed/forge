module;

#include <forge/contract/intrinsics.h>

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.contract.multi_index;

import forge.chain.protocol.fixed_key;
import forge.chain.protocol.values;
import forge.contract.intrinsics;
import forge.raw.codec;

export namespace forge::contract {

using chain::protocol::name;

inline constexpr name same_payer{};

template <class Class, class Type, Type (Class::*Member)() const> struct const_mem_fun {
   using result_type = std::remove_reference_t<Type>;

   template <class Chained>
      requires(!std::convertible_to<const Chained&, const Class&>)
   decltype(auto) operator()(const Chained& value) const {
      return (*this)(*value);
   }

   Type operator()(const Class& value) const {
      return (value.*Member)();
   }

   Type operator()(std::reference_wrapper<const Class> value) const {
      return (*this)(value.get());
   }

   Type operator()(std::reference_wrapper<Class> value) const {
      return (*this)(value.get());
   }
};

template <name::raw IndexName, class Extractor> struct indexed_by {
   static constexpr auto index_name = static_cast<std::uint64_t>(IndexName);
   using secondary_extractor_type = Extractor;
};

template <class Key>
concept primary_key =
    std::same_as<std::remove_cvref_t<Key>, std::uint64_t> || std::same_as<std::remove_cvref_t<Key>, name>;

template <class Key>
concept secondary_key =
    std::same_as<std::remove_cvref_t<Key>, std::uint64_t> ||
    std::same_as<std::remove_cvref_t<Key>, chain::protocol::uint128_t> ||
    std::same_as<std::remove_cvref_t<Key>, chain::protocol::key256> || std::same_as<std::remove_cvref_t<Key>, double> ||
    std::same_as<std::remove_cvref_t<Key>, long double>;

template <class Row>
concept table_row = std::default_initializable<Row> &&
                    requires(const Row& value) { requires primary_key<decltype(value.primary_key())>; };

namespace detail {

template <class Key>
concept primary_key_argument =
    (std::integral<std::remove_cvref_t<Key>> && !std::same_as<std::remove_cvref_t<Key>, bool>) ||
    std::same_as<std::remove_cvref_t<Key>, name>;

template <class Index, class Row>
concept secondary_index_spec =
    requires {
       typename Index::secondary_extractor_type;
       { Index::index_name } -> std::convertible_to<std::uint64_t>;
    } && std::invocable<typename Index::secondary_extractor_type, const Row&> &&
    secondary_key<std::invoke_result_t<typename Index::secondary_extractor_type, const Row&>>;

template <class... Index> consteval bool valid_index_names() {
   constexpr auto names = std::array<std::uint64_t, sizeof...(Index)>{Index::index_name...};
   for (std::size_t current = 0; current < names.size(); ++current) {
      if (names[current] == 0U || names[current] == chain::protocol::encode_name("primary")) {
         return false;
      }
      for (std::size_t previous = 0; previous < current; ++previous) {
         if (names[current] == names[previous]) {
            return false;
         }
      }
   }
   return true;
}

template <std::integral Key>
   requires(!std::same_as<std::remove_cvref_t<Key>, bool>)
inline std::uint64_t to_raw_key(Key value) noexcept {
   return static_cast<std::uint64_t>(value);
}

inline std::uint64_t to_raw_key(name value) noexcept {
   return value.value;
}

template <class Key> struct secondary_db;

#define FORGE_CONTRACT_SCALAR_SECONDARY_DB(key_type, prefix)                                                           \
   template <> struct secondary_db<key_type> {                                                                         \
      static std::int32_t store(std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t primary,  \
                                const key_type& secondary) {                                                           \
         return ::prefix##_store(scope, table, payer, primary, &secondary);                                            \
      }                                                                                                                \
                                                                                                                       \
      static void update(std::int32_t iterator, std::uint64_t payer, const key_type& secondary) {                      \
         ::prefix##_update(iterator, payer, &secondary);                                                               \
      }                                                                                                                \
                                                                                                                       \
      static void remove(std::int32_t iterator) {                                                                      \
         ::prefix##_remove(iterator);                                                                                  \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t next(std::int32_t iterator, std::uint64_t* primary) {                                        \
         return ::prefix##_next(iterator, primary);                                                                    \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t previous(std::int32_t iterator, std::uint64_t* primary) {                                    \
         return ::prefix##_previous(iterator, primary);                                                                \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table,                   \
                                       std::uint64_t primary, key_type& secondary) {                                   \
         return ::prefix##_find_primary(code, scope, table, &secondary, primary);                                      \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table,                 \
                                         const key_type& secondary, std::uint64_t& primary) {                          \
         return ::prefix##_find_secondary(code, scope, table, &secondary, &primary);                                   \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t lower_bound(std::uint64_t code, std::uint64_t scope, std::uint64_t table,                    \
                                      key_type& secondary, std::uint64_t& primary) {                                   \
         return ::prefix##_lowerbound(code, scope, table, &secondary, &primary);                                       \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t upper_bound(std::uint64_t code, std::uint64_t scope, std::uint64_t table,                    \
                                      key_type& secondary, std::uint64_t& primary) {                                   \
         return ::prefix##_upperbound(code, scope, table, &secondary, &primary);                                       \
      }                                                                                                                \
                                                                                                                       \
      static std::int32_t end(std::uint64_t code, std::uint64_t scope, std::uint64_t table) {                          \
         return ::prefix##_end(code, scope, table);                                                                    \
      }                                                                                                                \
   }

FORGE_CONTRACT_SCALAR_SECONDARY_DB(std::uint64_t, db_idx64);
FORGE_CONTRACT_SCALAR_SECONDARY_DB(chain::protocol::uint128_t, db_idx128);
FORGE_CONTRACT_SCALAR_SECONDARY_DB(double, db_idx_double);
FORGE_CONTRACT_SCALAR_SECONDARY_DB(long double, db_idx_long_double);

#undef FORGE_CONTRACT_SCALAR_SECONDARY_DB

template <> struct secondary_db<chain::protocol::key256> {
   using key_type = chain::protocol::key256;

   static std::int32_t store(std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t primary,
                             const key_type& secondary) {
      return ::db_idx256_store(scope, table, payer, primary, secondary.get_array().data(), key_type::num_words());
   }

   static void update(std::int32_t iterator, std::uint64_t payer, const key_type& secondary) {
      ::db_idx256_update(iterator, payer, secondary.get_array().data(), key_type::num_words());
   }

   static void remove(std::int32_t iterator) {
      ::db_idx256_remove(iterator);
   }

   static std::int32_t next(std::int32_t iterator, std::uint64_t* primary) {
      return ::db_idx256_next(iterator, primary);
   }

   static std::int32_t previous(std::int32_t iterator, std::uint64_t* primary) {
      return ::db_idx256_previous(iterator, primary);
   }

   static std::int32_t find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t primary,
                                    key_type& secondary) {
      auto words = secondary.get_array();
      const auto result = ::db_idx256_find_primary(code, scope, table, words.data(), words.size(), primary);
      secondary = key_type{words};
      return result;
   }

   static std::int32_t find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table,
                                      const key_type& secondary, std::uint64_t& primary) {
      return ::db_idx256_find_secondary(code, scope, table, secondary.get_array().data(), key_type::num_words(),
                                        &primary);
   }

   static std::int32_t lower_bound(std::uint64_t code, std::uint64_t scope, std::uint64_t table, key_type& secondary,
                                   std::uint64_t& primary) {
      auto words = secondary.get_array();
      const auto result = ::db_idx256_lowerbound(code, scope, table, words.data(), words.size(), &primary);
      secondary = key_type{words};
      return result;
   }

   static std::int32_t upper_bound(std::uint64_t code, std::uint64_t scope, std::uint64_t table, key_type& secondary,
                                   std::uint64_t& primary) {
      auto words = secondary.get_array();
      const auto result = ::db_idx256_upperbound(code, scope, table, words.data(), words.size(), &primary);
      secondary = key_type{words};
      return result;
   }

   static std::int32_t end(std::uint64_t code, std::uint64_t scope, std::uint64_t table) {
      return ::db_idx256_end(code, scope, table);
   }
};

template <secondary_key Key> constexpr Key lowest_secondary() noexcept {
   if constexpr (std::same_as<Key, double> || std::same_as<Key, long double>) {
      return -std::numeric_limits<Key>::infinity();
   } else {
      return Key{};
   }
}

template <secondary_key Key> bool secondary_equal(const Key& left, const Key& right) noexcept {
   if constexpr (std::same_as<Key, double> || std::same_as<Key, long double>) {
      using representation = std::array<std::byte, sizeof(Key)>;
      return std::bit_cast<representation>(left) == std::bit_cast<representation>(right);
   } else {
      return left == right;
   }
}

template <class Value, class Function> decltype(auto) with_packed(const Value& value, Function&& function) {
   constexpr auto stack_capacity = std::size_t{512U};
   const auto size = raw::pack_size(value);
   check(size <= std::numeric_limits<std::uint32_t>::max(), "serialized table row is too large");

   auto pack_into = [&](std::uint8_t* data) -> decltype(auto) {
      auto stream = forge::datastream<std::uint8_t*>{data, size};
      raw::pack(stream, value);
      return std::forward<Function>(function)(data, static_cast<std::uint32_t>(size));
   };

   if (size <= stack_capacity) {
      auto storage = std::array<std::uint8_t, stack_capacity>{};
      return pack_into(storage.data());
   }

   auto storage = std::vector<std::uint8_t>(size);
   return pack_into(storage.data());
}

template <class Function> decltype(auto) with_buffer(std::size_t size, Function&& function) {
   constexpr auto stack_capacity = std::size_t{512U};
   if (size <= stack_capacity) {
      auto storage = std::array<std::uint8_t, stack_capacity>{};
      return std::forward<Function>(function)(storage.data());
   }

   auto storage = std::vector<std::uint8_t>(size);
   return std::forward<Function>(function)(storage.data());
}

} // namespace detail

template <name::raw TableName, table_row T, class... Indices> class multi_index {
   static constexpr auto table_name = static_cast<std::uint64_t>(TableName);
   static constexpr auto index_count = sizeof...(Indices);
   static constexpr auto no_available_primary_key = std::numeric_limits<std::uint64_t>::max() - 1U;
   static constexpr auto unset_next_primary_key = std::numeric_limits<std::uint64_t>::max();

   static_assert(index_count <= 16U, "multi_index only supports a maximum of 16 secondary indices");
   static_assert(name{TableName}.length() < 13U,
                 "multi_index does not support table names with a length greater than 12");
   static_assert((detail::secondary_index_spec<Indices, T> && ...),
                 "multi_index secondary indices require a supported const extractor");
   static_assert(detail::valid_index_names<Indices...>(), "invalid index name used in multi_index");

   using index_specs = std::tuple<Indices...>;

   template <std::size_t Number> using index_spec = std::tuple_element_t<Number, index_specs>;
   template <std::size_t Number> using extractor_type = typename index_spec<Number>::secondary_extractor_type;
   template <std::size_t Number>
   using secondary_type = std::remove_cvref_t<std::invoke_result_t<extractor_type<Number>, const T&>>;

   template <std::size_t Number> static constexpr std::uint64_t secondary_table_name() noexcept {
      return (table_name & 0xfffffffffffffff0ULL) | (Number & 0x0fU);
   }

   template <std::size_t Number> static auto extract_secondary(const T& value) {
      static_assert(secondary_key<secondary_type<Number>>, "unsupported secondary key type");
      return extractor_type<Number>{}(value);
   }

   template <name::raw IndexName, std::size_t Number = 0U> static consteval std::size_t index_position() {
      if constexpr (Number == index_count) {
         return index_count;
      } else if constexpr (index_spec<Number>::index_name == static_cast<std::uint64_t>(IndexName)) {
         return Number;
      } else {
         return index_position<IndexName, Number + 1U>();
      }
   }

   struct item : T {
      explicit item(const multi_index* owner_value) : owner{owner_value} {
         secondary_iterators.fill(-1);
      }

      const multi_index* owner = nullptr;
      std::int32_t primary_iterator = -1;
      std::array<std::int32_t, index_count> secondary_iterators{};
   };

   template <std::size_t Number, bool IsConst> class secondary_index;

 public:
   class const_iterator {
    public:
      using iterator_category = std::bidirectional_iterator_tag;
      using iterator_concept = std::bidirectional_iterator_tag;
      using value_type = T;
      using difference_type = std::ptrdiff_t;
      using pointer = const T*;
      using reference = const T&;

      const_iterator() = default;

      reference operator*() const {
         check(value_ != nullptr, "object passed to iterator_to is not in multi_index");
         return *static_cast<const T*>(value_);
      }

      pointer operator->() const {
         return std::addressof(operator*());
      }

      const_iterator& operator++() {
         check(value_ != nullptr, "cannot increment end iterator");
         auto primary = std::uint64_t{};
         const auto next = ::db_next_i64(value_->primary_iterator, &primary);
         value_ = next < 0 ? nullptr : std::addressof(owner_->load(next));
         return *this;
      }

      const_iterator operator++(int) {
         auto result = *this;
         ++*this;
         return result;
      }

      const_iterator& operator--() {
         auto primary = std::uint64_t{};
         auto previous = std::int32_t{-1};
         if (value_ == nullptr) {
            const auto end = ::db_end_i64(owner_->code_.value, owner_->scope_, table_name);
            check(end != -1, "cannot decrement end iterator when the table is empty");
            previous = ::db_previous_i64(end, &primary);
            check(previous >= 0, "cannot decrement end iterator when the table is empty");
         } else {
            previous = ::db_previous_i64(value_->primary_iterator, &primary);
            check(previous >= 0, "cannot decrement iterator at beginning of table");
         }
         value_ = std::addressof(owner_->load(previous));
         return *this;
      }

      const_iterator operator--(int) {
         auto result = *this;
         --*this;
         return result;
      }

      friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept {
         return left.value_ == right.value_;
      }

    private:
      friend class multi_index;

      const_iterator(const multi_index* owner, const item* value = nullptr) : owner_{owner}, value_{value} {}

      const multi_index* owner_ = nullptr;
      const item* value_ = nullptr;
   };

   using const_reverse_iterator = std::reverse_iterator<const_iterator>;

   multi_index(name code, std::uint64_t scope) : code_{code}, scope_{scope} {}

   [[nodiscard]] name get_code() const noexcept {
      return code_;
   }

   [[nodiscard]] std::uint64_t get_scope() const noexcept {
      return scope_;
   }

   const_iterator cbegin() const {
      return lower_bound(std::uint64_t{});
   }

   const_iterator begin() const {
      return cbegin();
   }

   const_iterator cend() const {
      return const_iterator{this};
   }

   const_iterator end() const {
      return cend();
   }

   const_reverse_iterator crbegin() const {
      return std::make_reverse_iterator(cend());
   }

   const_reverse_iterator rbegin() const {
      return crbegin();
   }

   const_reverse_iterator crend() const {
      return std::make_reverse_iterator(cbegin());
   }

   const_reverse_iterator rend() const {
      return crend();
   }

   template <detail::primary_key_argument Key> const_iterator lower_bound(Key primary) const {
      const auto iterator = ::db_lowerbound_i64(code_.value, scope_, table_name, detail::to_raw_key(primary));
      return iterator < 0 ? end() : const_iterator{this, std::addressof(load(iterator))};
   }

   template <detail::primary_key_argument Key> const_iterator upper_bound(Key primary) const {
      const auto iterator = ::db_upperbound_i64(code_.value, scope_, table_name, detail::to_raw_key(primary));
      return iterator < 0 ? end() : const_iterator{this, std::addressof(load(iterator))};
   }

   template <detail::primary_key_argument Key> const_iterator find(Key primary) const {
      const auto raw_primary = detail::to_raw_key(primary);
      if (const auto* cached = find_cached(raw_primary); cached != nullptr) {
         return const_iterator{this, cached};
      }

      const auto iterator = ::db_find_i64(code_.value, scope_, table_name, raw_primary);
      return iterator < 0 ? end() : const_iterator{this, std::addressof(load(iterator))};
   }

   template <detail::primary_key_argument Key>
   const_iterator require_find(Key primary, const char* error_message = "unable to find key") const {
      const auto result = find(primary);
      check(result != end(), error_message);
      return result;
   }

   template <detail::primary_key_argument Key>
   const T& get(Key primary, const char* error_message = "unable to find key") const {
      return *require_find(primary, error_message);
   }

   const_iterator iterator_to(const T& value) const {
      const auto* found = find_cached(value);
      check(found != nullptr, "object passed to iterator_to is not in multi_index");
      return const_iterator{this, found};
   }

   [[nodiscard]] std::uint64_t available_primary_key() const {
      if (next_primary_key_ == unset_next_primary_key) {
         if (begin() == end()) {
            next_primary_key_ = 0U;
         } else {
            const auto last = --end();
            const auto primary = detail::to_raw_key(last->primary_key());
            next_primary_key_ = primary >= no_available_primary_key ? no_available_primary_key : primary + 1U;
         }
      }

      check(next_primary_key_ < no_available_primary_key, "next primary key in table is at autoincrement limit");
      return next_primary_key_;
   }

   template <name::raw IndexName> auto get_index() {
      constexpr auto position = index_position<IndexName>();
      static_assert(position < index_count, "name provided is not the name of any secondary index within multi_index");
      return secondary_index<position, false>{this};
   }

   template <name::raw IndexName> auto get_index() const {
      constexpr auto position = index_position<IndexName>();
      static_assert(position < index_count, "name provided is not the name of any secondary index within multi_index");
      return secondary_index<position, true>{this};
   }

   template <class Constructor> const_iterator emplace(name payer, Constructor&& constructor) {
      check(code_ == current_receiver(), "cannot create objects in table of another contract");

      auto& stored = items_.emplace_back(this);
      auto& value = static_cast<T&>(stored);
      std::forward<Constructor>(constructor)(value);
      const auto primary = detail::to_raw_key(value.primary_key());

      stored.primary_iterator = detail::with_packed(value, [&](const void* data, std::uint32_t size) {
         return ::db_store_i64(scope_, table_name, payer.value, primary, data, size);
      });
      store_secondary(stored, payer.value, primary, std::make_index_sequence<index_count>{});

      if (primary >= next_primary_key_) {
         next_primary_key_ = primary >= no_available_primary_key ? no_available_primary_key : primary + 1U;
      }
      return const_iterator{this, std::addressof(stored)};
   }

   template <class Updater> void modify(const_iterator iterator, name payer, Updater&& updater) {
      check(iterator != end(), "cannot pass end iterator to modify");
      modify(*iterator, payer, std::forward<Updater>(updater));
   }

   template <class Updater> void modify(const T& value, name payer, Updater&& updater) {
      auto* stored = find_cached(value);
      check(stored != nullptr, "object passed to modify is not in multi_index");
      check(code_ == current_receiver(), "cannot modify objects in table of another contract");

      const auto primary = detail::to_raw_key(value.primary_key());
      const auto old_secondary = secondary_values(value, std::make_index_sequence<index_count>{});
      std::forward<Updater>(updater)(static_cast<T&>(*stored));
      check(primary == detail::to_raw_key(value.primary_key()),
            "updater cannot change primary key when modifying an object");

      detail::with_packed(value, [&](const void* data, std::uint32_t size) {
         ::db_update_i64(stored->primary_iterator, payer.value, data, size);
      });
      update_secondary(*stored, payer.value, primary, old_secondary, std::make_index_sequence<index_count>{});

      if (primary >= next_primary_key_) {
         next_primary_key_ = primary >= no_available_primary_key ? no_available_primary_key : primary + 1U;
      }
   }

   const_iterator erase(const_iterator iterator) {
      check(iterator != end(), "cannot pass end iterator to erase");
      const auto& value = *iterator;
      ++iterator;
      erase(value);
      return iterator;
   }

   void erase(const T& value) {
      auto position = find_cached_position(value);
      check(position != items_.end(), "object passed to erase is not in multi_index");
      check(code_ == current_receiver(), "cannot erase objects in table of another contract");

      auto& stored = *position;
      const auto primary = detail::to_raw_key(stored.primary_key());
      ::db_remove_i64(stored.primary_iterator);
      remove_secondary(stored, primary, std::make_index_sequence<index_count>{});
      items_.erase(position);
   }

 private:
   template <std::size_t Number, bool IsConst> class secondary_index {
      using owner_type = std::conditional_t<IsConst, const multi_index, multi_index>;

    public:
      using secondary_extractor_type = extractor_type<Number>;
      using secondary_key_type = secondary_type<Number>;

      static_assert(secondary_key<secondary_key_type>, "unsupported secondary key type");
      class const_iterator {
       public:
         using iterator_category = std::bidirectional_iterator_tag;
         using iterator_concept = std::bidirectional_iterator_tag;
         using value_type = T;
         using difference_type = std::ptrdiff_t;
         using pointer = const T*;
         using reference = const T&;

         const_iterator() = default;

         reference operator*() const {
            check(value_ != nullptr, "object passed to iterator_to is not in multi_index");
            return *static_cast<const T*>(value_);
         }

         pointer operator->() const {
            return std::addressof(operator*());
         }

         const_iterator& operator++() {
            check(value_ != nullptr, "cannot increment end iterator");
            owner_->template ensure_secondary_iterator<Number>(*const_cast<item*>(value_));
            auto primary = std::uint64_t{};
            const auto next =
                detail::secondary_db<secondary_key_type>::next(value_->secondary_iterators[Number], &primary);
            if (next < 0) {
               value_ = nullptr;
            } else {
               auto& loaded = owner_->load_primary(primary);
               loaded.secondary_iterators[Number] = next;
               value_ = std::addressof(loaded);
            }
            return *this;
         }

         const_iterator operator++(int) {
            auto result = *this;
            ++*this;
            return result;
         }

         const_iterator& operator--() {
            auto primary = std::uint64_t{};
            auto previous = std::int32_t{-1};
            if (value_ == nullptr) {
               const auto end = detail::secondary_db<secondary_key_type>::end(owner_->code_.value, owner_->scope_,
                                                                              secondary_table_name<Number>());
               check(end != -1, "cannot decrement end iterator when the index is empty");
               previous = detail::secondary_db<secondary_key_type>::previous(end, &primary);
               check(previous >= 0, "cannot decrement end iterator when the index is empty");
            } else {
               owner_->template ensure_secondary_iterator<Number>(*const_cast<item*>(value_));
               previous =
                   detail::secondary_db<secondary_key_type>::previous(value_->secondary_iterators[Number], &primary);
               check(previous >= 0, "cannot decrement iterator at beginning of index");
            }

            auto& loaded = owner_->load_primary(primary);
            loaded.secondary_iterators[Number] = previous;
            value_ = std::addressof(loaded);
            return *this;
         }

         const_iterator operator--(int) {
            auto result = *this;
            --*this;
            return result;
         }

         friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept {
            return left.value_ == right.value_;
         }

       private:
         friend class secondary_index;

         const_iterator(const multi_index* owner, const item* value = nullptr) : owner_{owner}, value_{value} {}

         const multi_index* owner_ = nullptr;
         const item* value_ = nullptr;
      };

      using const_reverse_iterator = std::reverse_iterator<const_iterator>;

      const_iterator cbegin() const {
         return lower_bound(detail::lowest_secondary<secondary_key_type>());
      }

      const_iterator begin() const {
         return cbegin();
      }

      const_iterator cend() const {
         return const_iterator{owner_};
      }

      const_iterator end() const {
         return cend();
      }

      const_reverse_iterator crbegin() const {
         return std::make_reverse_iterator(cend());
      }

      const_reverse_iterator rbegin() const {
         return crbegin();
      }

      const_reverse_iterator crend() const {
         return std::make_reverse_iterator(cbegin());
      }

      const_reverse_iterator rend() const {
         return crend();
      }

      const_iterator find(const secondary_key_type& secondary) const {
         const auto result = lower_bound(secondary);
         if (result == end() || extract_secondary<Number>(*result) != secondary) {
            return end();
         }
         return result;
      }

      const_iterator find(secondary_key_type&& secondary) const {
         return find(secondary);
      }

      const_iterator require_find(const secondary_key_type& secondary,
                                  const char* error_message = "unable to find secondary key") const {
         const auto result = find(secondary);
         check(result != end(), error_message);
         return result;
      }

      const_iterator require_find(secondary_key_type&& secondary,
                                  const char* error_message = "unable to find secondary key") const {
         return require_find(secondary, error_message);
      }

      const T& get(const secondary_key_type& secondary,
                   const char* error_message = "unable to find secondary key") const {
         return *require_find(secondary, error_message);
      }

      const T& get(secondary_key_type&& secondary, const char* error_message = "unable to find secondary key") const {
         return get(secondary, error_message);
      }

      const_iterator lower_bound(const secondary_key_type& secondary) const {
         auto primary = std::uint64_t{};
         auto mutable_secondary = secondary;
         const auto iterator = detail::secondary_db<secondary_key_type>::lower_bound(
             owner_->code_.value, owner_->scope_, secondary_table_name<Number>(), mutable_secondary, primary);
         if (iterator < 0) {
            return end();
         }
         auto& loaded = owner_->load_primary(primary);
         loaded.secondary_iterators[Number] = iterator;
         return const_iterator{owner_, std::addressof(loaded)};
      }

      const_iterator lower_bound(secondary_key_type&& secondary) const {
         return lower_bound(secondary);
      }

      const_iterator upper_bound(const secondary_key_type& secondary) const {
         auto primary = std::uint64_t{};
         auto mutable_secondary = secondary;
         const auto iterator = detail::secondary_db<secondary_key_type>::upper_bound(
             owner_->code_.value, owner_->scope_, secondary_table_name<Number>(), mutable_secondary, primary);
         if (iterator < 0) {
            return end();
         }
         auto& loaded = owner_->load_primary(primary);
         loaded.secondary_iterators[Number] = iterator;
         return const_iterator{owner_, std::addressof(loaded)};
      }

      const_iterator upper_bound(secondary_key_type&& secondary) const {
         return upper_bound(secondary);
      }

      const_iterator iterator_to(const T& value) const {
         auto* stored = owner_->find_cached(value);
         check(stored != nullptr, "object passed to iterator_to is not in multi_index");
         owner_->template ensure_secondary_iterator<Number>(*stored);
         return const_iterator{owner_, stored};
      }

      template <class Updater>
         requires(!IsConst)
      void modify(const_iterator iterator, name payer, Updater&& updater) {
         check(iterator != end(), "cannot pass end iterator to modify");
         owner_->modify(*iterator, payer, std::forward<Updater>(updater));
      }

      template <class Updater>
         requires(!IsConst)
      void modify(const T& value, name payer, Updater&& updater) {
         owner_->modify(value, payer, std::forward<Updater>(updater));
      }

      const_iterator erase(const_iterator iterator)
         requires(!IsConst)
      {
         check(iterator != end(), "cannot pass end iterator to erase");
         const auto& value = *iterator;
         ++iterator;
         owner_->erase(value);
         return iterator;
      }

      [[nodiscard]] name get_code() const noexcept {
         return owner_->get_code();
      }

      [[nodiscard]] std::uint64_t get_scope() const noexcept {
         return owner_->get_scope();
      }

      [[nodiscard]] static constexpr std::uint64_t name() noexcept {
         return secondary_table_name<Number>();
      }

      [[nodiscard]] static constexpr std::uint64_t number() noexcept {
         return Number;
      }

    private:
      friend class multi_index;

      explicit secondary_index(owner_type* owner) : owner_{owner} {}

      owner_type* owner_ = nullptr;
   };

   item& load(std::int32_t iterator) const {
      for (auto& value : items_) {
         if (value.primary_iterator == iterator) {
            return value;
         }
      }

      const auto size = ::db_get_i64(iterator, nullptr, 0U);
      check(size >= 0, "error reading iterator");
      auto& result = items_.emplace_back(this);
      result.primary_iterator = iterator;
      detail::with_buffer(static_cast<std::size_t>(size), [&](std::uint8_t* data) {
         const auto read = ::db_get_i64(iterator, data, static_cast<std::uint32_t>(size));
         check(read == size, "error reading iterator");
         auto stream = forge::datastream<const std::uint8_t*>{data, static_cast<std::size_t>(size)};
         raw::unpack(stream, static_cast<T&>(result));
         check(stream.remaining() == 0U, "table row contains trailing bytes");
      });
      return result;
   }

   item& load_primary(std::uint64_t primary) const {
      if (auto* cached = find_cached(primary); cached != nullptr) {
         return *cached;
      }
      const auto iterator = ::db_find_i64(code_.value, scope_, table_name, primary);
      check(iterator >= 0, "unable to find key");
      return load(iterator);
   }

   item* find_cached(std::uint64_t primary) const noexcept {
      for (auto& value : items_) {
         if (detail::to_raw_key(value.primary_key()) == primary) {
            return std::addressof(value);
         }
      }
      return nullptr;
   }

   item* find_cached(const T& value) const noexcept {
      for (auto& candidate : items_) {
         if (static_cast<const T*>(std::addressof(candidate)) == std::addressof(value)) {
            return std::addressof(candidate);
         }
      }
      return nullptr;
   }

   typename std::list<item>::iterator find_cached_position(const T& value) const noexcept {
      for (auto position = items_.begin(); position != items_.end(); ++position) {
         if (static_cast<const T*>(std::addressof(*position)) == std::addressof(value)) {
            return position;
         }
      }
      return items_.end();
   }

   template <std::size_t Number> void ensure_secondary_iterator(item& value) const {
      if (value.secondary_iterators[Number] >= 0) {
         return;
      }
      auto secondary = secondary_type<Number>{};
      value.secondary_iterators[Number] = detail::secondary_db<secondary_type<Number>>::find_primary(
          code_.value, scope_, secondary_table_name<Number>(), detail::to_raw_key(value.primary_key()), secondary);
   }

   template <std::size_t... Number> static auto secondary_values(const T& value, std::index_sequence<Number...>) {
      return std::tuple<secondary_type<Number>...>{extract_secondary<Number>(value)...};
   }

   template <std::size_t... Number>
   void store_secondary(item& value, std::uint64_t payer, std::uint64_t primary, std::index_sequence<Number...>) {
      ((value.secondary_iterators[Number] = detail::secondary_db<secondary_type<Number>>::store(
            scope_, secondary_table_name<Number>(), payer, primary, extract_secondary<Number>(value))),
       ...);
   }

   template <class Tuple, std::size_t... Number>
   void update_secondary(item& value, std::uint64_t payer, std::uint64_t primary, const Tuple& old_values,
                         std::index_sequence<Number...>) {
      (
          [&] {
             const auto current = secondary_type<Number>{extract_secondary<Number>(value)};
             // CDT updates a secondary row, including its payer, only when its key changes.
             if (!detail::secondary_equal(std::get<Number>(old_values), current)) {
                ensure_secondary_iterator<Number>(value);
                check(value.secondary_iterators[Number] >= 0, "unable to find secondary key");
                detail::secondary_db<secondary_type<Number>>::update(value.secondary_iterators[Number], payer, current);
             }
          }(),
          ...);
   }

   template <std::size_t... Number>
   void remove_secondary(item& value, std::uint64_t primary, std::index_sequence<Number...>) {
      (
          [&] {
             if (value.secondary_iterators[Number] < 0) {
                auto secondary = secondary_type<Number>{};
                value.secondary_iterators[Number] = detail::secondary_db<secondary_type<Number>>::find_primary(
                    code_.value, scope_, secondary_table_name<Number>(), primary, secondary);
             }
             if (value.secondary_iterators[Number] >= 0) {
                detail::secondary_db<secondary_type<Number>>::remove(value.secondary_iterators[Number]);
             }
          }(),
          ...);
   }

   name code_{};
   std::uint64_t scope_ = 0U;
   mutable std::uint64_t next_primary_key_ = unset_next_primary_key;
   mutable std::list<item> items_;
};

} // namespace forge::contract

module;

#include <boost/describe.hpp>

#include <concepts>
#include <cstdint>
#include <type_traits>

export module forge.db.object.object;

import forge.ids.object_id;

export namespace forge::db::object {

enum class object_kind : std::uint8_t {
   application,
   system,
};

inline constexpr std::uint8_t system_space = 0;

template <typename Derived, std::uint8_t Space, std::uint16_t Type, object_kind Kind = object_kind::application>
struct object {
   static_assert(Kind == object_kind::system || Space != system_space,
                 "forge::db::object application objects cannot use the reserved system space");
   static_assert(Kind == object_kind::application || Space == system_space,
                 "forge::db::object system objects must use the reserved system space");

   using derived_type = Derived;
   using id_t = forge::ids::typed_id<Space, Type>;
   using object_base_type = object<Derived, Space, Type, Kind>;

   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;
   static constexpr object_kind kind = Kind;

   id_t id;

   bool operator==(const object&) const = default;
   auto operator<=>(const object&) const = default;

   BOOST_DESCRIBE_CLASS(object, (), (id), (), ())
};

template <typename Derived, std::uint16_t Type>
struct system_object : object<Derived, system_space, Type, object_kind::system> {
   using base_type = object<Derived, system_space, Type, object_kind::system>;

   BOOST_DESCRIBE_CLASS(system_object, (base_type), (), (), ())
};

template <typename T>
struct object_base_traits {
   static constexpr bool is_object_base = false;
};

template <typename Derived, std::uint8_t Space, std::uint16_t Type, object_kind Kind>
struct object_base_traits<object<Derived, Space, Type, Kind>> {
   static constexpr bool is_object_base = true;
   using derived_type = Derived;
   using id_t = forge::ids::typed_id<Space, Type>;
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;
   static constexpr object_kind kind = Kind;
};

template <typename T>
concept object_value = requires {
   typename std::remove_cvref_t<T>::object_base_type;
   typename std::remove_cvref_t<T>::id_t;
   { std::remove_cvref_t<T>::space } -> std::convertible_to<std::uint8_t>;
   { std::remove_cvref_t<T>::type } -> std::convertible_to<std::uint16_t>;
   { std::remove_cvref_t<T>::kind } -> std::convertible_to<object_kind>;
} && forge::ids::typed_id_traits<typename std::remove_cvref_t<T>::id_t>::is_typed_id &&
   object_base_traits<typename std::remove_cvref_t<T>::object_base_type>::is_object_base &&
   std::same_as<typename object_base_traits<typename std::remove_cvref_t<T>::object_base_type>::derived_type,
                std::remove_cvref_t<T>> &&
   std::derived_from<std::remove_cvref_t<T>, typename std::remove_cvref_t<T>::object_base_type>;

template <typename T>
concept application_object_value = object_value<T> && std::remove_cvref_t<T>::kind == object_kind::application;

template <typename T>
concept system_object_value = object_value<T> && std::remove_cvref_t<T>::kind == object_kind::system;

template <typename Id>
using object_index_for_id_t = forge::ids::type_for_id_t<Id>;

} // namespace forge::db::object

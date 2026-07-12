module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.db.object.header;

import forge.ids.object_id;
import forge.db.object.index;
import forge.db.object.object;

export namespace forge::db::object {

struct header : system_object<header, 0> {
   using base_type = system_object<header, 0>;

   static constexpr std::uint32_t current_version = 1;
   static constexpr std::uint32_t minimum_version = 1;

   std::uint32_t version = current_version;

   bool operator==(const header&) const = default;

   BOOST_DESCRIBE_CLASS(header, (base_type), (version), (), ())
};

struct header_by_id;

using header_index = object_index<header, indexed_by<primary_unique<header_by_id>>>;

inline constexpr auto header_id = header::id_t{0};

} // namespace forge::db::object

export namespace forge::ids {

template <> struct type_for_id<forge::db::object::header::id_t> {
   using type = forge::db::object::header_index;
};

} // namespace forge::ids

module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.db.object.header;

import forge.ids.object_id;
import forge.db.object.index;
import forge.db.object.object;

export namespace forge::db::object {

struct header : system_object<header, system::type_id::header> {
   using base_type = system_object<header, system::type_id::header>;

   static constexpr std::uint32_t current_version = 1;
   static constexpr std::uint32_t minimum_version = 1;

   std::uint32_t version = current_version;

   bool operator==(const header&) const = default;

   BOOST_DESCRIBE_CLASS(header, (base_type), (version), (), ())
};

struct header_by_id;

using header_index = object_index<header, indexed_by<primary_unique<header_by_id>>>;

inline constexpr auto header_id = header::id_t{0};

template <> struct index_for_id<header::id_t> {
   using type = header_index;
};

} // namespace forge::db::object

module;

#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.db.revision.types;

import forge.db.object.index;
import forge.db.object.object;
import forge.db.object.system;
import forge.ids.object_id;

export namespace forge::db::revision {

using revision_id_t = std::uint64_t;

struct prune_options {
   std::uint64_t max_revisions = 0;
   std::uint64_t max_deltas = 0;
};

struct prune_result {
   std::uint64_t revisions_pruned = 0;
   std::uint64_t deltas_pruned = 0;
   std::optional<revision_id_t> last_pruned;
   bool complete = false;
};

struct retention_guard_address {
   std::string family;
   std::vector<std::byte> key;

   bool operator==(const retention_guard_address&) const = default;
};

BOOST_DESCRIBE_STRUCT(retention_guard_address, (), (family, key))

struct state : forge::db::object::system_object<state, forge::db::object::system::type_id::revision_state> {
   using base_type = forge::db::object::system_object<
      state, forge::db::object::system::type_id::revision_state>;

   static constexpr std::uint32_t current_format_version = 1;

   std::uint32_t format_version = current_format_version;
   revision_id_t next_revision = 1;
   std::optional<revision_id_t> head;
   revision_id_t prune_baseline = 0;
   revision_id_t oldest_retained = 1;
   std::uint64_t next_delta = 1;

   bool operator==(const state&) const = default;

   BOOST_DESCRIBE_CLASS(state, (base_type),
                        (format_version, next_revision, head, prune_baseline, oldest_retained, next_delta), (), ())
};

struct entry : forge::db::object::system_object<entry, forge::db::object::system::type_id::revision_entry> {
   using base_type = forge::db::object::system_object<
      entry, forge::db::object::system::type_id::revision_entry>;

   std::optional<revision_id_t> parent;
   std::uint64_t first_delta = 0;
   std::uint64_t delta_count = 0;

   bool operator==(const entry&) const = default;

   BOOST_DESCRIBE_CLASS(entry, (base_type), (parent, first_delta, delta_count), (), ())
};

struct delta : forge::db::object::system_object<delta, forge::db::object::system::type_id::revision_delta> {
   using base_type = forge::db::object::system_object<
      delta, forge::db::object::system::type_id::revision_delta>;

   revision_id_t revision = 0;
   std::uint64_t ordinal = 0;
   std::string family;
   std::vector<std::byte> key;
   std::optional<std::vector<std::byte>> before;
   std::optional<retention_guard_address> retention_guard;

   bool operator==(const delta&) const = default;

   BOOST_DESCRIBE_CLASS(delta, (base_type),
                        (revision, ordinal, family, key, before, retention_guard), (), ())
};

struct state_by_id;
struct entry_by_id;
struct delta_by_id;

using state_index = forge::db::object::object_index<
   state, forge::db::object::indexed_by<forge::db::object::primary_unique<state_by_id>>>;
using entry_index = forge::db::object::object_index<
   entry, forge::db::object::indexed_by<forge::db::object::primary_unique<entry_by_id>>>;
using delta_index = forge::db::object::object_index<
   delta, forge::db::object::indexed_by<forge::db::object::primary_unique<delta_by_id>>>;

inline constexpr auto state_id = state::id_t{0};

} // namespace forge::db::revision

export namespace forge::db::object {

template <> struct index_for_id<forge::db::revision::state::id_t> {
   using type = forge::db::revision::state_index;
};

template <> struct index_for_id<forge::db::revision::entry::id_t> {
   using type = forge::db::revision::entry_index;
};

template <> struct index_for_id<forge::db::revision::delta::id_t> {
   using type = forge::db::revision::delta_index;
};

} // namespace forge::db::object

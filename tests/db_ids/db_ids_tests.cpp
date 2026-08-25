#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <vector>

import forge.db.ids.object_id;
import forge.db.ids.typed_id;
import forge.exceptions;
import forge.raw.raw;
import forge.variant.value;

namespace {

struct fake_object;

using fake_id = forge::db::ids::typed_id<9, 42>;

} // namespace

namespace forge::db::ids {

template <> struct type_for_id<fake_id> {
   using type = fake_object;
};

} // namespace forge::db::ids

namespace {

forge::raw::bytes pack_object_id(forge::db::ids::object_id value) {
   return forge::raw::pack(value);
}

forge::db::ids::object_id unpack_object_id(const forge::raw::bytes& bytes) {
   return forge::raw::unpack<forge::db::ids::object_id>(bytes);
}

template <forge::db::ids::typed_id_like Id>
forge::raw::bytes pack_typed_id(Id value) {
   return forge::raw::pack(value);
}

template <forge::db::ids::typed_id_like Id>
Id unpack_typed_id(const forge::raw::bytes& bytes) {
   return forge::raw::unpack<Id>(bytes);
}

} // namespace

BOOST_AUTO_TEST_SUITE(db_ids_test_suite)

BOOST_AUTO_TEST_CASE(db_ids_object_id_raw_roundtrip_preserves_field_order) {
   const auto original = forge::db::ids::object_id{.space = 7, .type = 513, .instance = 0x0102030405060708ULL};
   const auto packed = pack_object_id(original);

   BOOST_REQUIRE_EQUAL(packed.size(), 11U);
   BOOST_CHECK_EQUAL(packed[0], 7U);
   BOOST_CHECK_EQUAL(packed[1], 1U);
   BOOST_CHECK_EQUAL(packed[2], 2U);

   const auto decoded = unpack_object_id(packed);
   BOOST_CHECK(decoded == original);
}

BOOST_AUTO_TEST_CASE(db_ids_object_id_variant_roundtrip_preserves_object_shape) {
   const auto original = forge::db::ids::object_id{.space = 3, .type = 9, .instance = 42};
   auto encoded = forge::variant{};
   to_variant(original, encoded);

   auto decoded = forge::db::ids::object_id{};
   from_variant(encoded, decoded);

   BOOST_CHECK(decoded == original);
}

BOOST_AUTO_TEST_CASE(db_ids_typed_id_converts_to_and_from_object_id) {
   using account_id = forge::db::ids::typed_id<1, 2>;

   const auto typed = account_id{99};
   const auto generic = forge::db::ids::to_object_id(typed);

   BOOST_CHECK_EQUAL(generic.space, 1U);
   BOOST_CHECK_EQUAL(generic.type, 2U);
   BOOST_CHECK_EQUAL(generic.instance, 99U);
   BOOST_CHECK((forge::db::ids::matches<1, 2>(generic)));
   static_assert(forge::db::ids::typed_id_like<account_id>);
   static_assert(forge::db::ids::typed_id_traits<account_id>::space == 1);
   static_assert(forge::db::ids::typed_id_traits<account_id>::type == 2);

   const auto maybe_typed = forge::db::ids::try_typed<1, 2>(generic);
   BOOST_REQUIRE(maybe_typed.has_value());
   BOOST_CHECK(*maybe_typed == typed);
}

BOOST_AUTO_TEST_CASE(db_ids_typed_id_raw_roundtrip_preserves_instance_wire_bytes) {
   using account_id = forge::db::ids::typed_id<1, 2>;
   const auto original = account_id{0x0102030405060708ULL};
   const auto packed = pack_typed_id(original);

   BOOST_REQUIRE_EQUAL(packed.size(), 8U);
   const auto expected = std::array<std::uint8_t, 8>{8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U};
   BOOST_CHECK_EQUAL_COLLECTIONS(packed.begin(), packed.end(), expected.begin(), expected.end());
   BOOST_CHECK(unpack_typed_id<account_id>(packed) == original);
}

BOOST_AUTO_TEST_CASE(db_ids_try_typed_rejects_mismatched_space_or_type) {
   const auto mismatch = forge::db::ids::object_id{.space = 1, .type = 3, .instance = 99};

   BOOST_CHECK((!forge::db::ids::try_typed<1, 2>(mismatch).has_value()));
   BOOST_CHECK((!forge::db::ids::try_typed<1, 2>(forge::db::ids::object_id{.space = 9, .type = 2, .instance = 99})
                     .has_value()));
}

BOOST_AUTO_TEST_CASE(db_ids_object_id_variant_rejects_out_of_range_fields_with_forge_error) {
   auto encoded = forge::variant{};
   encoded = forge::mutable_variant_object{}("space", std::uint64_t{256U})("type", std::uint64_t{2U})(
       "instance", std::uint64_t{99U});

   auto decoded = forge::db::ids::object_id{};
   BOOST_CHECK_THROW(from_variant(encoded, decoded), forge::exceptions::context_error);
}

BOOST_AUTO_TEST_CASE(db_ids_to_string_uses_space_type_instance) {
   const auto generic = forge::db::ids::object_id{.space = 5, .type = 17, .instance = 1234};
   BOOST_CHECK_EQUAL(forge::db::ids::to_string(generic), "5/17/1234");

   const auto typed = forge::db::ids::typed_id<5, 17>{1234};
   BOOST_CHECK_EQUAL(forge::db::ids::to_string(typed), "5/17/1234");
}

BOOST_AUTO_TEST_CASE(db_ids_type_for_id_maps_typed_ids_to_domain_types) {
   static_assert(std::same_as<forge::db::ids::type_for_id_t<fake_id>, fake_object>);
}

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <span>

import forge.chain.quorum.evaluate;

namespace {

namespace quorum = forge::chain::quorum;

BOOST_AUTO_TEST_CASE(chain_quorum_evaluates_weighted_signers) {
   const auto weights = std::array<std::uint64_t, 3>{2U, 3U, 5U};
   const auto reached = quorum::evaluate(7U, weights, std::array<std::uint32_t, 2>{0U, 2U});
   BOOST_TEST(reached.reached());
   BOOST_TEST(reached.total_weight == 10U);
   BOOST_TEST(reached.signed_weight == 7U);

   const auto insufficient = quorum::evaluate(8U, weights, std::array<std::uint32_t, 2>{0U, 2U});
   BOOST_TEST(!insufficient.reached());
   BOOST_TEST(insufficient.signed_weight == 7U);
}

BOOST_AUTO_TEST_CASE(chain_quorum_zero_threshold_is_reached) {
   const auto weights = std::array<std::uint64_t, 2>{0U, 0U};
   const auto value = quorum::evaluate(0U, weights, std::span<const std::uint32_t>{});
   BOOST_TEST(value.reached());
   BOOST_TEST(value.total_weight == 0U);
   BOOST_TEST(value.signed_weight == 0U);
}

BOOST_AUTO_TEST_CASE(chain_quorum_rejects_invalid_signer_sets) {
   const auto weights = std::array<std::uint64_t, 2>{1U, 2U};
   BOOST_CHECK_THROW(static_cast<void>(
                         quorum::evaluate(1U, weights, std::array<std::uint32_t, 2>{1U, 1U})),
                     quorum::exceptions::duplicate_signer);
   BOOST_CHECK_THROW(static_cast<void>(
                         quorum::evaluate(1U, weights, std::array<std::uint32_t, 1>{2U})),
                     quorum::exceptions::signer_out_of_range);
}

BOOST_AUTO_TEST_CASE(chain_quorum_rejects_total_weight_overflow) {
   const auto weights =
       std::array<std::uint64_t, 2>{std::numeric_limits<std::uint64_t>::max(), 1U};
   BOOST_CHECK_THROW(static_cast<void>(
                         quorum::evaluate(1U, weights, std::span<const std::uint32_t>{})),
                     quorum::exceptions::weight_overflow);
}

} // namespace

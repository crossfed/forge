#include <boost/test/unit_test.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

import forge.chain.fork.graph;

namespace {

namespace fork = forge::chain::fork;

using graph_type = fork::graph<std::uint64_t, std::uint64_t, std::uint64_t>;

void reset_graph(graph_type& graph) {
   graph.reset({.id = 1U, .parent = 0U, .rank = 1U, .value = 10U});
}

BOOST_AUTO_TEST_CASE(chain_fork_handles_graph_lifecycle) {
   auto graph = graph_type{};
   reset_graph(graph);
   BOOST_TEST(fork::inserted(graph.insert({.id = 2U, .parent = 1U, .rank = 2U, .value = 20U})));
   BOOST_TEST(fork::inserted(graph.insert({.id = 3U, .parent = 1U, .rank = 4U, .value = 30U})));
   BOOST_TEST(fork::inserted(graph.insert({.id = 4U, .parent = 3U, .rank = 3U, .value = 40U})));
   BOOST_TEST(static_cast<int>(
                  graph.insert({.id = 4U, .parent = 3U, .rank = 3U, .value = 40U})) ==
              static_cast<int>(fork::insert_status::duplicate));
   BOOST_TEST(static_cast<int>(
                  graph.insert({.id = 5U, .parent = 99U, .rank = 5U, .value = 50U})) ==
              static_cast<int>(fork::insert_status::unlinked));

   BOOST_TEST(graph.best().id == 3U);
   BOOST_TEST(graph.common_ancestor(2U, 4U) == 1U);
   BOOST_TEST(graph.is_ancestor(1U, 4U));
   BOOST_TEST(!graph.is_ancestor(2U, 4U));
   const auto path = graph.path(1U, 4U);
   BOOST_REQUIRE_EQUAL(path.size(), 2U);
   BOOST_TEST(path[0].id == 3U);
   BOOST_TEST(path[1].id == 4U);

   graph.replace({.id = 4U, .parent = 3U, .rank = 3U, .value = 41U});
   BOOST_TEST(graph.find(4U)->value == 41U);
   BOOST_CHECK_THROW(graph.replace({.id = 4U, .parent = 3U, .rank = 6U, .value = 42U}),
                     fork::exceptions::invalid_node);

   const auto removed = graph.remove_subtree(3U);
   BOOST_TEST(removed.size() == 2U);
   BOOST_TEST(graph.best().id == 2U);
   BOOST_TEST(!graph.contains(4U));
   BOOST_CHECK_THROW(static_cast<void>(graph.remove_subtree(1U)), fork::exceptions::invalid_root);
}

BOOST_AUTO_TEST_CASE(chain_fork_advances_root_and_prunes_competing_branches) {
   auto graph = graph_type{};
   reset_graph(graph);
   BOOST_TEST(fork::inserted(graph.insert({.id = 2U, .parent = 1U, .rank = 2U, .value = 20U})));
   BOOST_TEST(fork::inserted(graph.insert({.id = 3U, .parent = 2U, .rank = 4U, .value = 30U})));
   BOOST_TEST(fork::inserted(graph.insert({.id = 4U, .parent = 1U, .rank = 3U, .value = 40U})));

   const auto removed = graph.advance_root(2U);
   BOOST_TEST(removed.size() == 2U);
   BOOST_TEST(graph.root().id == 2U);
   BOOST_TEST(graph.root().parent == 0U);
   BOOST_TEST(graph.best().id == 3U);
   BOOST_TEST(!graph.contains(1U));
   BOOST_TEST(!graph.contains(4U));
}

BOOST_AUTO_TEST_CASE(chain_fork_breaks_equal_rank_ties_by_greater_id) {
   auto graph = graph_type{};
   reset_graph(graph);
   BOOST_TEST(fork::inserted(graph.insert({.id = 2U, .parent = 1U, .rank = 5U, .value = 20U})));
   BOOST_TEST(fork::inserted(graph.insert({.id = 3U, .parent = 1U, .rank = 5U, .value = 30U})));
   BOOST_TEST(graph.best().id == 3U);
}

struct counted_better {
   std::atomic_size_t* comparisons = nullptr;

   bool operator()(const std::uint64_t& left, const std::uint64_t& right) const noexcept {
      comparisons->fetch_add(1U, std::memory_order_relaxed);
      return left > right;
   }
};

BOOST_AUTO_TEST_CASE(chain_fork_best_does_not_compare_all_nodes) {
   auto comparisons = std::atomic_size_t{};
   auto graph = fork::graph<std::uint64_t, std::uint64_t, std::uint64_t, counted_better>{
       counted_better{.comparisons = &comparisons}};
   graph.reset({.id = 1U, .parent = 0U, .rank = 1U, .value = 1U});
   for (auto id = std::uint64_t{2}; id <= 1000U; ++id) {
      BOOST_REQUIRE(fork::inserted(graph.insert({.id = id, .parent = 1U, .rank = id, .value = id})));
   }

   comparisons.store(0U, std::memory_order_relaxed);
   BOOST_TEST(graph.best().id == 1000U);
   BOOST_TEST(comparisons.load(std::memory_order_relaxed) == 0U);
}

struct throwing_better {
   std::shared_ptr<std::uint64_t> rejected_rank;

   bool operator()(const std::uint64_t& left, const std::uint64_t& right) const {
      if (left == *rejected_rank || right == *rejected_rank) {
         throw std::runtime_error{"rank comparison failed"};
      }
      return left > right;
   }
};

BOOST_AUTO_TEST_CASE(chain_fork_insert_has_strong_exception_safety) {
   const auto rejected_rank = std::make_shared<std::uint64_t>(0U);
   auto graph = fork::graph<std::uint64_t, std::uint64_t, std::uint64_t, throwing_better>{
       throwing_better{.rejected_rank = rejected_rank}};
   graph.reset({.id = 1U, .parent = 0U, .rank = 1U, .value = 10U});
   BOOST_REQUIRE(fork::inserted(
       graph.insert({.id = 2U, .parent = 1U, .rank = 2U, .value = 20U})));

   *rejected_rank = 99U;
   BOOST_CHECK_THROW(
       static_cast<void>(graph.insert({.id = 3U, .parent = 1U, .rank = 99U, .value = 30U})),
       std::runtime_error);
   *rejected_rank = 0U;

   BOOST_TEST(graph.size() == 2U);
   BOOST_TEST(!graph.contains(3U));
   BOOST_TEST(graph.best().id == 2U);
}

BOOST_AUTO_TEST_CASE(chain_fork_remove_prepares_rank_lookups_before_mutation) {
   const auto rejected_rank = std::make_shared<std::uint64_t>(0U);
   auto graph = fork::graph<std::uint64_t, std::uint64_t, std::uint64_t, throwing_better>{
       throwing_better{.rejected_rank = rejected_rank}};
   graph.reset({.id = 1U, .parent = 0U, .rank = 1U, .value = 10U});
   BOOST_REQUIRE(fork::inserted(
       graph.insert({.id = 2U, .parent = 1U, .rank = 2U, .value = 20U})));

   *rejected_rank = 2U;
   BOOST_CHECK_THROW(static_cast<void>(graph.remove_subtree(2U)), std::runtime_error);
   *rejected_rank = 0U;

   BOOST_TEST(graph.size() == 2U);
   BOOST_TEST(graph.contains(2U));
   BOOST_TEST(graph.best().id == 2U);
}

struct throwing_value {
   std::shared_ptr<bool> reject_copy;
   std::uint64_t value = 0;

   throwing_value(std::shared_ptr<bool> reject, std::uint64_t current)
       : reject_copy{std::move(reject)}, value{current} {}

   throwing_value(const throwing_value& other)
       : reject_copy{other.reject_copy}, value{other.value} {
      if (*reject_copy) {
         throw std::runtime_error{"value copy failed"};
      }
   }

   throwing_value(throwing_value&&) noexcept = default;
   throwing_value& operator=(const throwing_value&) = default;
   throwing_value& operator=(throwing_value&&) noexcept = default;
};

BOOST_AUTO_TEST_CASE(chain_fork_remove_has_strong_exception_safety) {
   const auto reject_copy = std::make_shared<bool>(false);
   auto graph = fork::graph<std::uint64_t, std::uint64_t, throwing_value>{};
   graph.reset({.id = 1U,
                .parent = 0U,
                .rank = 1U,
                .value = throwing_value{reject_copy, 10U}});
   BOOST_REQUIRE(fork::inserted(graph.insert(
       {.id = 2U, .parent = 1U, .rank = 2U, .value = throwing_value{reject_copy, 20U}})));

   *reject_copy = true;
   BOOST_CHECK_THROW(static_cast<void>(graph.remove_subtree(2U)), std::runtime_error);
   *reject_copy = false;

   BOOST_TEST(graph.size() == 2U);
   BOOST_TEST(graph.contains(2U));
   BOOST_TEST(graph.best().id == 2U);
}

BOOST_AUTO_TEST_CASE(chain_fork_supports_concurrent_reads_and_writes) {
   auto graph = graph_type{};
   reset_graph(graph);
   auto failed = std::atomic_bool{false};
   auto writer = std::thread{[&] {
      for (auto id = std::uint64_t{2}; id <= 200U; ++id) {
         if (!fork::inserted(graph.insert({.id = id, .parent = 1U, .rank = id, .value = id}))) {
            failed.store(true, std::memory_order_relaxed);
         }
      }
   }};
   auto reader = std::thread{[&] {
      while (graph.size() < 200U) {
         const auto best = graph.best();
         if (!graph.contains(best.id)) {
            failed.store(true, std::memory_order_relaxed);
         }
      }
   }};
   writer.join();
   reader.join();

   BOOST_TEST(!failed.load(std::memory_order_relaxed));
   BOOST_TEST(graph.best().id == 200U);
}

BOOST_AUTO_TEST_CASE(chain_fork_reports_empty_and_unknown_nodes) {
   auto graph = graph_type{};
   BOOST_CHECK_THROW(static_cast<void>(graph.best()), fork::exceptions::empty_graph);
   graph.reset({.id = 1U, .parent = 0U, .rank = 1U, .value = 1U});
   BOOST_CHECK_THROW(static_cast<void>(graph.find(2U).value()), std::bad_optional_access);
   BOOST_CHECK_THROW(static_cast<void>(graph.path(2U, 1U)), fork::exceptions::unknown_node);
}

} // namespace

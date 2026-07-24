#include <cstdint>

import forge.chain.fork.graph;

int main() {
   auto graph = forge::chain::fork::graph<std::uint64_t, std::uint64_t, std::uint64_t>{};
   graph.reset({.id = 1U, .parent = 0U, .rank = 1U, .value = 1U});
   if (!forge::chain::fork::inserted(graph.insert({.id = 2U, .parent = 1U, .rank = 2U, .value = 2U}))) {
      return 1;
   }
   return graph.best().id == 2U ? 0 : 2;
}

#include <type_traits>

import forge.contract.graph;

int main() {
   static_assert(std::is_default_constructible_v<forge::contract::graph::descriptor>);
   return 0;
}

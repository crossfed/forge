#include <type_traits>

import forge.contract.manifest.generator;

int main() {
   static_assert(std::is_default_constructible_v<forge::contract::manifest::request>);
   return 0;
}

#include <type_traits>

import forge.tooling.manifest.generator;

int main() {
   static_assert(std::is_default_constructible_v<forge::tooling::manifest::request>);
   return 0;
}

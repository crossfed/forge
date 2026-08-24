#include <type_traits>

import forge.tooling.abi.generator;

int main() {
   static_assert(std::is_default_constructible_v<forge::tooling::abi::request>);
   return 0;
}

#include <type_traits>

import forge.contract.abi.generator;

int main() {
   static_assert(std::is_default_constructible_v<forge::contract::abi::request>);
   return 0;
}

#include <type_traits>

import forge.contract.validation.validator;

int main() {
   static_assert(std::is_default_constructible_v<forge::contract::validation::request>);
   return 0;
}

#include <type_traits>

import forge.tooling.validation.validator;

int main() {
   static_assert(std::is_default_constructible_v<forge::tooling::validation::request>);
   return 0;
}

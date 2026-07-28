#include <cassert>

import product.chain.protocol;

int main() {
   const auto result = product::chain::checked_add(20, 22);
   assert(result && *result == 42);
}

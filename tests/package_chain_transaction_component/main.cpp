#include <cstdint>

import forge.chain.transaction.types;

int main() {
   const auto options = forge::chain::transaction::options{};
   return options.expiration_seconds == std::uint32_t{30} ? 0 : 1;
}

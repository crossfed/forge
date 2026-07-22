#include <cstdint>
#include <vector>

import forge.crypto.core.secret_bytes;

int main() {
   auto secret = forge::crypto::core::secret_bytes{std::vector<std::uint8_t>{1, 2, 3}};
   return secret.size() == 3 ? 0 : 1;
}

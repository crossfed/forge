#include <cstdint>
#include <vector>

import forge.crypto.core.secret_bytes;
import forge.crypto.core.secret_string;

int main() {
   auto secret = forge::crypto::core::secret_bytes{std::vector<std::uint8_t>{1, 2, 3}};
   auto text = forge::crypto::core::secret_string{"private material"};
   return secret.size() == 3 && text.view() == "private material" ? 0 : 1;
}

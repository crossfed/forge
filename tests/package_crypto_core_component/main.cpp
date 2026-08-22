#include <cstdint>
#include <array>
#include <vector>

import forge.crypto.core.constant_time;
import forge.crypto.core.secret_bytes;
import forge.crypto.core.secret_string;

int main() {
   auto secret = forge::crypto::core::secret_bytes{std::vector<std::uint8_t>{1, 2, 3}};
   auto text = forge::crypto::core::secret_string{"private material"};
   const auto left = std::array<std::uint8_t, 3>{1, 2, 3};
   const auto right = std::array<std::uint8_t, 3>{1, 2, 3};
   return secret.size() == 3 && text.view() == "private material" &&
                  forge::crypto::core::constant_time_equal(left, right)
              ? 0
              : 1;
}

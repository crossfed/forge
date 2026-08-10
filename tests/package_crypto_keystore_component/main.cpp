#include <cstddef>

import forge.crypto.keystore.types;
import forge.crypto.keystore.password;

int main() {
   const auto options = forge::crypto::keystore::store_options{};
   const auto request = forge::crypto::keystore::password_request{};
   return options.max_keys == std::size_t{1'024} && request.source == forge::crypto::keystore::password_source::terminal
              ? 0
              : 1;
}

#pragma once

#include <string>
#include <string_view>

namespace forge::tests::p2p {

struct identity_fixture {
   std::string certificate_pem;
   std::string private_key_pem;
};

[[nodiscard]] identity_fixture make_identity_fixture(std::string_view common_name);

} // namespace forge::tests::p2p

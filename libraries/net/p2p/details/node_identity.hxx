#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace forge::net::p2p {

struct libp2p_identity_material {
   std::optional<forge::crypto::asymmetric::private_key> private_key;
   std::vector<std::uint8_t> public_key;
};

[[nodiscard]] libp2p_identity_material make_libp2p_identity_material(const node::options& options);
[[nodiscard]] const forge::crypto::asymmetric::private_key&
require_libp2p_identity_private_key(const libp2p_identity_material& identity);

} // namespace forge::net::p2p

module;

#include <forge/exceptions/macros.hpp>

module forge.net.p2p.private_network;

namespace forge::net::p2p::private_network {

void validate(const options& value) {
   if (!value.protector) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P private-network profile requires a pnet protector");
   }
}

} // namespace forge::net::p2p::private_network

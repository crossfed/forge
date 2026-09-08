module;

#include <memory>

export module forge.net.p2p.private_network;

export import forge.net.p2p.exceptions;
export import forge.net.pnet.protector;

export namespace forge::net::p2p::private_network {

struct options {
   std::shared_ptr<const forge::net::pnet::protector> protector;
};

void validate(const options& value);

} // namespace forge::net::p2p::private_network

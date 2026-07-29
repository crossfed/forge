module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.crypto.asymmetric;
import forge.crypto.pki.pem;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/identity_signature.hxx"
#include "details/libp2p_identity_material.hxx"

namespace forge::net::p2p {

libp2p_identity_material make_libp2p_identity_material(const node::options& options) {
   if (options.private_key_pem.empty()) {
      return {
          .private_key = std::nullopt,
          .public_key = options.public_key,
      };
   }

   try {
      auto private_key = forge::crypto::pki::pem::read_private_key(options.private_key_pem);
      auto public_key = options.public_key;
      if (public_key.empty()) {
         public_key = encode_public_key(public_key_from_crypto(private_key.get_public_key()));
      }
      return {
          .private_key = std::move(private_key),
          .public_key = std::move(public_key),
      };
   } catch (const forge::exceptions::base& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, error.what());
   }
}

const forge::crypto::asymmetric::private_key&
require_libp2p_identity_private_key(const libp2p_identity_material& identity) {
   if (!identity.private_key) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p requires identity private key material");
   }
   return *identity.private_key;
}

} // namespace forge::net::p2p

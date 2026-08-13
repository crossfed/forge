#pragma once

namespace forge::crypto::signer {

struct configured_provider::impl {
   impl(key_id value_id, asymmetric::private_key value_private_key,
        asymmetric::public_key value_public_key);

   key_id id;
   asymmetric::private_key private_key;
   asymmetric::public_key public_key;
};

} // namespace forge::crypto::signer

#pragma once

namespace forge::crypto::bls::signer {

struct configured_provider::impl {
   explicit impl(private_key&& value);

   private_key key;
   description identity;
};

} // namespace forge::crypto::bls::signer

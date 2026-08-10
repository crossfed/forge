module;

#include <string>
#include <vector>

export module forge.crypto.signer.types;

export import forge.crypto.asymmetric;
export import forge.crypto.digest.sha256;

export namespace forge::crypto::signer {

struct key_id {
   std::string value;

   bool operator==(const key_id&) const = default;
   auto operator<=>(const key_id&) const = default;
};

struct key_info {
   key_id id;
   asymmetric::public_key public_key;

   bool operator==(const key_info&) const = default;
};

struct sign_digest_request {
   key_id id;
   digest::sha256 digest;

   bool operator==(const sign_digest_request&) const = default;
};

struct sign_digest_response {
   asymmetric::public_key public_key;
   asymmetric::signature signature;

   bool operator==(const sign_digest_response&) const = default;
};

} // namespace forge::crypto::signer

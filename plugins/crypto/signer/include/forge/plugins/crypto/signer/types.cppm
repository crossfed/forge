module;

#include <boost/describe.hpp>

#include <optional>
#include <string>
#include <vector>

export module forge.plugins.crypto.signer.types;

import forge.crypto.asymmetric;
import forge.crypto.digest.sha256;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

export namespace forge::plugins::crypto::signer {

struct plugin_options {
   std::vector<forge::crypto::asymmetric::text_encoding_profile> profiles;
};

struct key {
   std::string id;
   std::string private_key;
   std::string input_profile = "forge";
   std::vector<std::string> purposes;
};

struct config {
   std::vector<key> keys;
};

struct request {
   std::string key_id;
   std::string purpose;
   forge::crypto::digest::sha256 digest;
   std::optional<forge::crypto::asymmetric::algorithm> required_algorithm;
};

struct options {
   std::string purpose;
   std::optional<forge::crypto::asymmetric::algorithm> required_algorithm;
};

struct response {
   std::string key_id;
   forge::crypto::asymmetric::public_key public_key;
   forge::crypto::asymmetric::signature signature;
};

BOOST_DESCRIBE_STRUCT(key, (), (id, private_key, input_profile, purposes))
BOOST_DESCRIBE_STRUCT(config, (), (keys))
BOOST_DESCRIBE_STRUCT(request, (), (key_id, purpose, digest, required_algorithm))
BOOST_DESCRIBE_STRUCT(options, (), (purpose, required_algorithm))
BOOST_DESCRIBE_STRUCT(response, (), (key_id, public_key, signature))

} // namespace forge::plugins::crypto::signer

export template <> struct forge::schema::rules<forge::plugins::crypto::signer::key> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::crypto::signer::key> define() {
      auto schema = forge::schema::object<forge::plugins::crypto::signer::key>();
      schema.field<&forge::plugins::crypto::signer::key::id>("id").required().non_empty();
      schema.field<&forge::plugins::crypto::signer::key::private_key>("private-key")
         .required()
         .non_empty()
         .secret()
         .description("Private key material in one of the configured input profiles");
      schema.field<&forge::plugins::crypto::signer::key::input_profile>("input-profile").default_value("forge");
      schema.field<&forge::plugins::crypto::signer::key::purposes>("purposes")
         .min_items(1)
         .each_non_empty()
         .description("Allowed signature purposes for this key");
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::crypto::signer::config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::crypto::signer::config> define() {
      auto schema = forge::schema::object<forge::plugins::crypto::signer::config>();
      schema.field<&forge::plugins::crypto::signer::config::keys>("keys")
         .items<forge::plugins::crypto::signer::key>()
         .secret()
         .unique_by<&forge::plugins::crypto::signer::key::id>()
         .description("Local crypto signer key entries");
      return schema;
   }
};

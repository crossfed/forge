#pragma once

namespace forge::plugins::crypto::signer {

struct loaded_key {
   std::string key_id;
   forge::crypto::asymmetric::private_key private_key;
   std::vector<std::string> purposes;
};

} // namespace forge::plugins::crypto::signer

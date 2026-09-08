module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <vector>

export module forge.crypto.bls.signer.provider;

export import forge.crypto.bls;

export namespace forge::crypto::bls::signer {

struct description {
   public_key key;
   signature proof_of_possession;

   bool operator==(const description&) const = default;
};

struct sign_result {
   public_key key;
   signature value;

   bool operator==(const sign_result&) const = default;
};

class provider {
 public:
   virtual ~provider();

   [[nodiscard]] virtual boost::asio::awaitable<description> describe() = 0;
   [[nodiscard]] virtual boost::asio::awaitable<sign_result> sign(std::vector<std::uint8_t> message) = 0;
};

} // namespace forge::crypto::bls::signer

module;

#include <boost/asio/awaitable.hpp>
#include <vector>

export module forge.crypto.signer.provider;

export import forge.crypto.signer.exceptions;
export import forge.crypto.signer.types;

export namespace forge::crypto::signer {

class provider {
 public:
   virtual ~provider();

   [[nodiscard]] virtual boost::asio::awaitable<std::vector<key_info>> keys() = 0;
   [[nodiscard]] virtual boost::asio::awaitable<key_info> describe(const key_id& id) = 0;
   [[nodiscard]] virtual boost::asio::awaitable<sign_digest_response> sign_digest(sign_digest_request request) = 0;
};

} // namespace forge::crypto::signer

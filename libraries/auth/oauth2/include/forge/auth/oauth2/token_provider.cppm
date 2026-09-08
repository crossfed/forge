module;

#include <boost/asio/awaitable.hpp>

export module forge.auth.oauth2.token_provider;

export import forge.auth.oauth2.access_token;
export import forge.auth.oauth2.exceptions;
export import forge.auth.oauth2.types;

export namespace forge::auth::oauth2 {

class token_provider {
 public:
   virtual ~token_provider();

   [[nodiscard]] virtual boost::asio::awaitable<access_token> async_access_token(token_request request) = 0;
};

} // namespace forge::auth::oauth2

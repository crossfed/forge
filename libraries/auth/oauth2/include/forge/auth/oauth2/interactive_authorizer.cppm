module;

#include <boost/asio/awaitable.hpp>

export module forge.auth.oauth2.interactive_authorizer;

export import forge.auth.oauth2.exceptions;
export import forge.auth.oauth2.types;

export namespace forge::auth::oauth2 {

class interactive_authorizer {
 public:
   virtual ~interactive_authorizer();

   virtual boost::asio::awaitable<void> async_authorize(token_request request) = 0;
   virtual boost::asio::awaitable<void> async_revoke() = 0;
   virtual boost::asio::awaitable<void> async_invalidate() = 0;
};

} // namespace forge::auth::oauth2

module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

export module forge.auth.appauth.provider;

export import forge.auth.oauth2.interactive_authorizer;
export import forge.auth.oauth2.token_provider;

export namespace forge::auth::appauth {

struct provider_options {
   std::string issuer;
   std::string client_id;
   forge::auth::oauth2::scope_set scopes;
   std::optional<std::string> audience;
   std::string keychain_service;
   std::string keychain_account;
   std::string authorization_success_url = "https://openid.github.io/AppAuth-iOS/redirect/";
};

class provider final : public forge::auth::oauth2::token_provider, public forge::auth::oauth2::interactive_authorizer {
 public:
   [[nodiscard]] static std::shared_ptr<provider> create(provider_options options);

   ~provider() override;

   provider(const provider&) = delete;
   provider& operator=(const provider&) = delete;

   [[nodiscard]] boost::asio::awaitable<void> async_authorize(forge::auth::oauth2::token_request request) override;
   [[nodiscard]] boost::asio::awaitable<void> async_revoke() override;
   [[nodiscard]] boost::asio::awaitable<void> async_invalidate() override;
   [[nodiscard]] boost::asio::awaitable<forge::auth::oauth2::access_token>
   async_access_token(forge::auth::oauth2::token_request request) override;

 private:
   struct impl;

   explicit provider(std::shared_ptr<impl> implementation);

   std::shared_ptr<impl> impl_;
};

} // namespace forge::auth::appauth

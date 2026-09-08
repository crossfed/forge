module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <memory>
#include <string>

export module forge.auth.oidc_agent.client;

export import forge.asio.compute;
export import forge.auth.oidc_agent.exceptions;
export import forge.auth.oauth2.token_provider;

export namespace forge::auth::oidc_agent {

enum class selection_kind {
   account,
   issuer,
};

struct account_selection {
   selection_kind kind = selection_kind::account;
   std::string value;
};

struct client_options {
   account_selection selection;
   std::string application_hint;
};

class client final : public forge::auth::oauth2::token_provider {
 public:
   [[nodiscard]] static std::shared_ptr<client> create(client_options options,
                                                       forge::asio::compute::executor blocking_executor);

   ~client() override;

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   [[nodiscard]] boost::asio::awaitable<forge::auth::oauth2::access_token>
   async_access_token(forge::auth::oauth2::token_request request) override;

 private:
   struct impl;

   explicit client(std::shared_ptr<impl> implementation);

   std::shared_ptr<impl> impl_;
};

} // namespace forge::auth::oidc_agent

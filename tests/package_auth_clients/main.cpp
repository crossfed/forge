#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <memory>

import forge.auth.appauth.provider;
import forge.auth.oauth2.access_token;
import forge.auth.oauth2.interactive_authorizer;
import forge.auth.oauth2.token_provider;
import forge.auth.workload.subject_token_source;

int main() {
   static_assert(std::derived_from<forge::auth::appauth::provider, forge::auth::oauth2::token_provider>);
   static_assert(std::derived_from<forge::auth::appauth::provider, forge::auth::oauth2::interactive_authorizer>);
   static_assert(
       std::same_as<decltype(std::declval<forge::auth::workload::subject_token_source&>().async_subject_token()),
                    boost::asio::awaitable<forge::auth::workload::subject_token>>);
   static_assert(!std::copy_constructible<forge::auth::oauth2::access_token>);
   const auto provider = forge::auth::appauth::provider::create({
       .issuer = "https://issuer.example",
       .client_id = "forge-package-test",
       .scopes = {"openid"},
       .keychain_service = "forge.package.auth",
       .keychain_account = "test",
   });
   return provider ? 0 : 1;
}

#include <concepts>

import forge.asio.compute;
import forge.auth.oidc_agent.client;
import forge.auth.oauth2.token_provider;

int main() {
   static_assert(std::derived_from<forge::auth::oidc_agent::client, forge::auth::oauth2::token_provider>);
   auto blocking = forge::asio::compute::pool{{
       .worker_threads = 1,
       .max_pending_tasks = 0,
       .max_waiting_submissions = 4,
       .thread_name = "forge-oidc-package",
   }};
   const auto client = forge::auth::oidc_agent::client::create(
       {
           .selection =
               {
                   .kind = forge::auth::oidc_agent::selection_kind::account,
                   .value = "package-test",
               },
           .application_hint = "forge-package-test",
       },
       blocking.get_executor());
   return client ? 0 : 1;
}

#pragma once

namespace forge::auth::oidc_agent {

struct client::impl {
   impl(client_options value, forge::asio::compute::executor blocking_executor);

   client_options options;
   forge::asio::compute::executor blocking;
};

} // namespace forge::auth::oidc_agent

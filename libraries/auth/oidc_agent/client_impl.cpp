module;

#include <utility>

module forge.auth.oidc_agent.client;

#include "details/client_impl.hxx"

namespace forge::auth::oidc_agent {

client::impl::impl(client_options value, forge::asio::compute::executor blocking_executor)
    : options{std::move(value)}, blocking{std::move(blocking_executor)} {}

} // namespace forge::auth::oidc_agent

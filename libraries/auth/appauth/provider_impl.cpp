module;

#include "details/appauth_backend.hxx"

#include <memory>
#include <mutex>
#include <utility>

module forge.auth.appauth.provider;

#include "details/provider_impl.hxx"

namespace forge::auth::appauth {

provider::impl::impl(provider_options options_value, std::shared_ptr<detail::appauth_backend> backend_value)
    : options{std::move(options_value)}, backend{std::move(backend_value)} {}

bool provider::impl::begin_authorization() {
   const auto lock = std::scoped_lock{mutex};
   if (authorization_in_progress) {
      return false;
   }

   authorization_in_progress = true;
   return true;
}

void provider::impl::finish_authorization() {
   const auto lock = std::scoped_lock{mutex};
   authorization_in_progress = false;
}

} // namespace forge::auth::appauth

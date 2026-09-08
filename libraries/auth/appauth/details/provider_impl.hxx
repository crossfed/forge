#pragma once

namespace forge::auth::appauth {

struct provider::impl {
   impl(provider_options options_value, std::shared_ptr<detail::appauth_backend> backend_value);

   [[nodiscard]] bool begin_authorization();
   void finish_authorization();

   provider_options options;
   std::shared_ptr<detail::appauth_backend> backend;
   std::mutex mutex;
   bool authorization_in_progress = false;
};

} // namespace forge::auth::appauth

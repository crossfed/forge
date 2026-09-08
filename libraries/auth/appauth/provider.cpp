module;

#include "details/appauth_backend.hxx"

#include <forge/exceptions/macros.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

module forge.auth.appauth.provider;

import forge.auth.oauth2.exceptions;
import forge.crypto.core.secret_string;

#include "details/provider_impl.hxx"

namespace forge::auth::appauth {
namespace {

enum class failure_code {
   invalid_options,
   keychain_failure,
   setup_required,
   token_unavailable,
   operation_in_progress,
   canceled,
};

[[noreturn]] void throw_failure(failure_code code) {
   switch (code) {
   case failure_code::invalid_options:
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::invalid_request, "AppAuth provider options are invalid");
   case failure_code::keychain_failure:
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::token_unavailable, "AppAuth Keychain operation failed");
   case failure_code::setup_required:
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::setup_required, "AppAuth authorization setup is required");
   case failure_code::token_unavailable:
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::token_unavailable,
                            "AppAuth did not return an access token");
   case failure_code::operation_in_progress:
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::token_unavailable,
                            "AppAuth authorization is already in progress");
   case failure_code::canceled:
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::canceled, "AppAuth operation was canceled");
   }
   std::terminate();
}

[[noreturn]] void throw_backend_failure(detail::backend_error error) {
   switch (error) {
   case detail::backend_error::invalid_options:
      throw_failure(failure_code::invalid_options);
   case detail::backend_error::keychain_failure:
      throw_failure(failure_code::keychain_failure);
   case detail::backend_error::setup_required:
      throw_failure(failure_code::setup_required);
   case detail::backend_error::token_unavailable:
      throw_failure(failure_code::token_unavailable);
   case detail::backend_error::canceled:
      throw_failure(failure_code::canceled);
   case detail::backend_error::none:
      break;
   }
   std::terminate();
}

void validate_text(const std::string& value) {
   if (value.empty() || value.find('\0') != std::string::npos) {
      throw_failure(failure_code::invalid_options);
   }
}

void validate_options(const provider_options& options) {
   validate_text(options.issuer);
   validate_text(options.client_id);
   validate_text(options.keychain_service);
   validate_text(options.keychain_account);
   validate_text(options.authorization_success_url);
   if (options.audience.has_value()) {
      validate_text(*options.audience);
   }
   for (const auto& scope : options.scopes) {
      validate_text(scope);
   }
}

void validate_request(const provider_options& options, const forge::auth::oauth2::token_request& request) {
   if (request.minimum_validity.count() < 0 || (!request.scopes.empty() && request.scopes != options.scopes) ||
       request.audience != options.audience) {
      FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::unsupported_request,
                            "AppAuth provider cannot satisfy the requested token constraints");
   }
}

struct callback_state : std::enable_shared_from_this<callback_state> {
   explicit callback_state(boost::asio::any_io_executor executor_value)
       : executor{std::move(executor_value)}, timer{executor} {
      timer.expires_at((boost::asio::steady_timer::time_point::max)());
   }

   [[nodiscard]] std::shared_ptr<detail::backend_operation> complete(detail::backend_result result_value) {
      std::shared_ptr<detail::backend_operation> operation;
      {
         const auto lock = std::scoped_lock{mutex};
         if (completed) {
            return {};
         }
         completed = true;
         result = std::move(result_value);
         operation = std::move(operation_);
      }
      boost::asio::post(executor, [state = shared_from_this()] {
         state->timer.expires_at((boost::asio::steady_timer::time_point::min)());
      });
      return operation;
   }

   [[nodiscard]] bool set_operation(std::shared_ptr<detail::backend_operation> operation_value) {
      const auto lock = std::scoped_lock{mutex};
      if (completed) {
         return true;
      }
      operation_ = std::move(operation_value);
      return false;
   }

   [[nodiscard]] detail::backend_result take_result() {
      const auto lock = std::scoped_lock{mutex};
      return std::move(*result);
   }

   boost::asio::any_io_executor executor;
   boost::asio::steady_timer timer;
   std::mutex mutex;
   std::optional<detail::backend_result> result;
   std::shared_ptr<detail::backend_operation> operation_;
   bool completed = false;
};

template <typename Start> boost::asio::awaitable<detail::backend_result> await_backend(Start&& start) {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto state = std::make_shared<callback_state>(executor);
   const auto inherited_cancellation = co_await boost::asio::this_coro::cancellation_state;
   auto cancellation_slot = inherited_cancellation.slot();
   const auto clear_cancellation_slot = [](boost::asio::cancellation_slot* slot) noexcept { slot->clear(); };
   auto cancellation_slot_cleanup = std::unique_ptr<boost::asio::cancellation_slot, decltype(clear_cancellation_slot)>{
       &cancellation_slot, clear_cancellation_slot};

   if (inherited_cancellation.cancelled() != boost::asio::cancellation_type::none) {
      co_return detail::backend_result{.error = detail::backend_error::canceled};
   }

   if (cancellation_slot.is_connected()) {
      cancellation_slot.assign([state](boost::asio::cancellation_type) {
         auto operation = state->complete(detail::backend_result{.error = detail::backend_error::canceled});
         if (operation) {
            operation->cancel();
         }
      });
   }

   const auto complete = [state](detail::backend_result result) {
      static_cast<void>(state->complete(std::move(result)));
   };
   auto operation = start(complete);
   if (state->set_operation(operation)) {
      operation->cancel();
   }

   auto ignored = boost::system::error_code{};
   co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ignored));

   if (inherited_cancellation.cancelled() != boost::asio::cancellation_type::none) {
      auto operation = state->complete(detail::backend_result{.error = detail::backend_error::canceled});
      if (operation) {
         operation->cancel();
      }
   }
   co_return state->take_result();
}

[[nodiscard]] detail::backend_options to_backend_options(const provider_options& options) {
   return {
       .issuer = options.issuer,
       .client_id = options.client_id,
       .scopes = options.scopes,
       .audience = options.audience,
       .keychain_service = options.keychain_service,
       .keychain_account = options.keychain_account,
       .authorization_success_url = options.authorization_success_url,
   };
}

} // namespace

provider::provider(std::shared_ptr<impl> implementation) : impl_{std::move(implementation)} {}

provider::~provider() = default;

std::shared_ptr<provider> provider::create(provider_options options) {
   validate_options(options);
   auto backend = detail::make_appauth_backend(to_backend_options(options));
   return std::shared_ptr<provider>{new provider{std::make_shared<impl>(std::move(options), std::move(backend))}};
}

boost::asio::awaitable<void> provider::async_authorize(forge::auth::oauth2::token_request request) {
   auto state = impl_;
   validate_request(state->options, request);
   if (!state->begin_authorization()) {
      throw_failure(failure_code::operation_in_progress);
   }
   try {
      const auto result = co_await await_backend([state](detail::backend_completion completion) {
         return state->backend->begin_authorize(std::move(completion));
      });
      if (result.error != detail::backend_error::none) {
         throw_backend_failure(result.error);
      }
   } catch (...) {
      state->finish_authorization();
      throw;
   }
   state->finish_authorization();
}

boost::asio::awaitable<void> provider::async_revoke() {
   static_cast<void>(co_await boost::asio::this_coro::executor);
   FORGE_THROW_EXCEPTION(forge::auth::oauth2::exceptions::unsupported_request,
                         "AppAuth 3.0.0 does not expose issuer-side token revocation");
}

boost::asio::awaitable<void> provider::async_invalidate() {
   auto state = impl_;
   const auto result = co_await await_backend([state](detail::backend_completion completion) {
      return state->backend->begin_invalidate(std::move(completion));
   });
   if (result.error != detail::backend_error::none) {
      throw_backend_failure(result.error);
   }
}

boost::asio::awaitable<forge::auth::oauth2::access_token>
provider::async_access_token(forge::auth::oauth2::token_request request) {
   auto state = impl_;
   validate_request(state->options, request);
   auto result = co_await await_backend(
       [state, minimum_validity = request.minimum_validity](detail::backend_completion completion) {
          return state->backend->begin_access_token(minimum_validity, std::move(completion));
       });
   if (result.error != detail::backend_error::none) {
      throw_backend_failure(result.error);
   }
   if (!result.token.has_value()) {
      throw_failure(failure_code::token_unavailable);
   }

   auto value = std::move(*result.token);
   auto metadata = forge::auth::oauth2::access_token_metadata{
       .issuer = std::move(value.issuer),
       .expires_at = value.expires_at,
   };
   co_return forge::auth::oauth2::access_token{
       forge::crypto::core::secret_string{std::move(value.token).take()},
       std::move(metadata),
   };
}

} // namespace forge::auth::appauth

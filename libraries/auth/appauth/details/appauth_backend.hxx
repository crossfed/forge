#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace forge::auth::appauth::detail {

enum class backend_error {
   none,
   invalid_options,
   keychain_failure,
   setup_required,
   token_unavailable,
   canceled,
};

// This private value owns a short-lived token until provider.cpp moves it into secret_string.
class secret_value {
 public:
   secret_value() = default;
   explicit secret_value(std::string value) noexcept;
   ~secret_value();

   secret_value(const secret_value&) = delete;
   secret_value& operator=(const secret_value&) = delete;
   secret_value(secret_value&& other) noexcept;
   secret_value& operator=(secret_value&& other) noexcept;

   [[nodiscard]] std::string take() && noexcept;

 private:
   std::string value_;
};

struct token_value {
   secret_value token;
   std::string issuer;
   std::optional<std::chrono::system_clock::time_point> expires_at;
};

struct backend_result {
   backend_error error = backend_error::none;
   std::optional<token_value> token;
};

using backend_completion = std::function<void(backend_result)>;

struct backend_options {
   std::string issuer;
   std::string client_id;
   std::vector<std::string> scopes;
   std::optional<std::string> audience;
   std::string keychain_service;
   std::string keychain_account;
   std::string authorization_success_url;
};

class backend_operation {
 public:
   virtual ~backend_operation() = default;

   virtual void cancel() noexcept = 0;
};

class appauth_backend {
 public:
   virtual ~appauth_backend() = default;

   [[nodiscard]] virtual std::shared_ptr<backend_operation> begin_authorize(backend_completion completion) = 0;
   [[nodiscard]] virtual std::shared_ptr<backend_operation> begin_invalidate(backend_completion completion) = 0;
   [[nodiscard]] virtual std::shared_ptr<backend_operation> begin_access_token(std::chrono::seconds minimum_validity,
                                                                               backend_completion completion) = 0;
};

[[nodiscard]] std::shared_ptr<appauth_backend> make_appauth_backend(backend_options options);

} // namespace forge::auth::appauth::detail

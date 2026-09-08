module;

#include <optional>
#include <string>

export module forge.auth.oauth2.access_token;

export import forge.auth.oauth2.types;
export import forge.crypto.core.secret_string;

export namespace forge::auth::oauth2 {

struct access_token_metadata {
   std::string issuer;
   std::optional<time_point> expires_at;
};

class access_token {
 public:
   explicit access_token(forge::crypto::core::secret_string secret, access_token_metadata metadata = {});
   ~access_token();

   access_token(const access_token&) = delete;
   access_token& operator=(const access_token&) = delete;
   access_token(access_token&&) noexcept;
   access_token& operator=(access_token&&) noexcept;

   [[nodiscard]] const forge::crypto::core::secret_string& secret() const noexcept;
   [[nodiscard]] const access_token_metadata& metadata() const noexcept;

 private:
   forge::crypto::core::secret_string secret_;
   access_token_metadata metadata_;
};

} // namespace forge::auth::oauth2

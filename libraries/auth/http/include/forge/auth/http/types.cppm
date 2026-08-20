module;

#include <chrono>
#include <optional>
#include <string>
#include <vector>

export module forge.auth.http.types;

export import forge.auth.session.types;
export import forge.crypto.core.secret_string;
export import forge.net.http.types;

export namespace forge::auth::http {

struct cookie_names {
   std::string session = "__Host-forge-session";
   std::string csrf = "__Host-forge-csrf";
   std::string pre_session = "__Host-forge-pre-session";
};

struct cookie_policy {
   cookie_names names;
   std::chrono::seconds pre_session_max_age = std::chrono::seconds{300};
   std::chrono::seconds session_max_age = std::chrono::seconds{28'800};
};

struct origin_policy {
   std::vector<std::string> allowed_origins;
};

struct browser_request_evidence {
   forge::net::http::method method = forge::net::http::method::unknown;
   std::vector<std::string> cookie_headers;
   std::vector<std::string> origin_headers;
   std::vector<std::string> csrf_headers;
};

struct session_evidence {
   forge::net::http::method method = forge::net::http::method::unknown;
   std::optional<std::string> origin;
   forge::crypto::core::secret_string session_token;
   std::optional<forge::crypto::core::secret_string> csrf_cookie;
   std::optional<forge::crypto::core::secret_string> csrf_header;
};

struct authorization_options {
   origin_policy origins;
   std::string required_scope;
   forge::auth::session::time_point now{};
};

struct security_header_options {
   bool sensitive_response = false;
};

} // namespace forge::auth::http

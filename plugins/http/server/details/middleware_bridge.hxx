#pragma once

namespace forge::plugins::http::server {

[[nodiscard]] forge::net::http::middleware_descriptor to_http_middleware(middleware_descriptor descriptor);

} // namespace forge::plugins::http::server

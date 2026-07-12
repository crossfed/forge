#pragma once

namespace forge::plugins::http::server {

struct pending_binding {
   forge::api::http::binding_plan binding;
   publish_options options;
};

} // namespace forge::plugins::http::server

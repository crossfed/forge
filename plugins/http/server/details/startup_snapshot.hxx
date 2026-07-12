#pragma once

#include "pending_binding.hxx"

namespace forge::plugins::http::server {

struct startup_snapshot {
   std::vector<pending_binding> bindings;
   std::vector<middleware_descriptor> middleware;
};

} // namespace forge::plugins::http::server

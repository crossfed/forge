#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

#include <memory>
#include <string>
#include <utility>

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.registry;
import forge.api.core.binding;

class local_request {
 public:
   explicit local_request(std::string value) : value_(std::make_unique<std::string>(std::move(value))) {}
   local_request(local_request&&) noexcept = default;
   local_request(const local_request&) = delete;

 private:
   std::unique_ptr<std::string> value_;
};

class local_response {
 public:
   explicit local_response(std::string value) : value_(std::make_unique<std::string>(std::move(value))) {}
   local_response(local_response&&) noexcept = default;
   local_response(const local_response&) = delete;

 private:
   std::unique_ptr<std::string> value_;
};

class local_api : public forge::api::core::contract<local_api> {
 public:
   virtual ~local_api() = default;
   virtual boost::asio::awaitable<local_response> transform(local_request request) = 0;
};

FORGE_API(local_api, FORGE_API_CONTRACT("package.local", 1, 0), FORGE_API_METHOD(transform))

int main() {
   auto registry = forge::api::core::registry{};
   const auto plan = std::move(forge::api::core::binding().serve(registry)).build();
   const auto available = forge::api::core::descriptor{
       .id = {.value = "package.smoke"},
       .version = {.major = 1, .revision = 2},
   };
   const auto requested = forge::api::core::api_ref{
       .id = {.value = "package.smoke"},
       .major = 1,
       .min_revision = 2,
   };
   const auto local = local_api::describe();
   const auto* transform = forge::api::core::find_method(local, "transform");
   return forge::api::core::compatible(available, requested) && plan.local == &registry && transform != nullptr &&
                  !transform->raw_invoker
              ? 0
              : 1;
}

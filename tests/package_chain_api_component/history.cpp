#include <concepts>
#include <cstddef>
#include <string>
#include <type_traits>

import forge.api.core.descriptor;
import forge.api.core.handle;
import forge.api.http.binding;
import forge.api.http.mapping;
import forge.chain.api.exceptions;
import forge.chain.api.history;
import forge.chain.api.raw_client;

bool history_api_package_coverage() {
   namespace chain_api = forge::chain::api;

   static_assert(std::is_abstract_v<chain_api::history>);
   static_assert(std::same_as<decltype(chain_api::service_handles{}.history_queries),
                              forge::api::core::handle<chain_api::history>>);
   const auto descriptor = chain_api::history::describe();
   const auto routes = forge::api::http::traits<chain_api::history>::routes();
   auto history_methods = std::size_t{0};
   for (const auto& method : descriptor.methods) {
      history_methods += method.name == "get_transaction" || method.name == "get_transaction_trace" ||
                         method.name == "get_block_traces" || method.name == "get_account_actions";
   }

   auto missing_handle_rejected = false;
   auto client = chain_api::raw_client{chain_api::service_handles{}};
   try {
      static_cast<void>(client.history());
   } catch (const chain_api::exceptions::unavailable&) {
      missing_handle_rejected = true;
   }

   return descriptor.id.value == "forge.chain.api.history" && descriptor.version.major == 1U &&
          descriptor.methods.size() == 4U && routes.size() == 4U && history_methods == 4U && missing_handle_rejected;
}

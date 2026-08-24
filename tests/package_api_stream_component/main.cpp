#include <cstdint>

import forge.api.stream.options;
import forge.api.stream.server;
import forge.api.stream.session;
import forge.api.core.trusted_invocation;

int main() {
   auto options = forge::api::stream::options{};
   auto session = forge::api::stream::session{};
   auto dispatch = forge::api::core::dispatch_options{
      .trusted = forge::api::core::trusted_invocation_builder{}.set(std::uint32_t{1}).build(),
   };
   (void)options;
   (void)session;
   (void)dispatch;
   return 0;
}

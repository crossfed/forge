import forge.plugins.p2p.resolver.managed_api;
import forge.plugins.p2p.resolver.plugin;

int main() {
   const auto descriptor = forge::plugins::p2p::resolver::managed_api::describe();
   const auto plugin = forge::plugins::p2p::resolver::descriptor();
   return descriptor.id.value == "forge.plugins.p2p.resolver.managed" && plugin.id.value == "forge.plugins.p2p.resolver"
              ? 0
              : 1;
}

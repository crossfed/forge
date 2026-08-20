import forge.net.http.assets;
import forge.plugins.http.server.plugin;
import forge.plugins.http.server.api;

int main() {
   const auto descriptor = forge::plugins::http::server::descriptor();
   const auto mount = forge::net::http::asset_mount{.path = "/admin"};
   return descriptor.id.value == "forge.plugins.http.server" && forge::plugins::http::server::api::ref().major == 2U &&
                  mount.path == "/admin"
              ? 0
              : 1;
}

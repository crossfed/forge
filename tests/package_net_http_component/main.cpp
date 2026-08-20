import forge.net.http.assets;
import forge.net.http.cookie;
import forge.net.http.types;

int main() {
   const auto formatted = forge::net::http::format_set_cookie({.name = "session", .value = "value"});
   const auto mount = forge::net::http::asset_mount{.path = "/admin"};
   return formatted == "session=value" && mount.path == "/admin" ? 0 : 1;
}

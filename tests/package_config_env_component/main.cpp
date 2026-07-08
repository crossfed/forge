#include <string>

import forge.config.env;

int main() {
   const auto name = forge::config::env::variable_name("http", "bind-port", forge::config::env::write_options{.prefix = "FORGE"});
   return name == "FORGE_HTTP_BIND_PORT" ? 0 : 1;
}

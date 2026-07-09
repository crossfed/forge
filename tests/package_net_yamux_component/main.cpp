import forge.net.yamux.options;

int main() {
   const auto options = forge::net::yamux::options{};
   return options.max_streams == 0 ? 1 : 0;
}

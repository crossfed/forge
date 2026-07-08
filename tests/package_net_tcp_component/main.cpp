import forge.net.tcp.options;

int main() {
   const auto options = forge::net::tcp::options{};
   return options.read_chunk_size == 0 ? 1 : 0;
}

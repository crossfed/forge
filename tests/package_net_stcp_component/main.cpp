import forge.net.stcp.options;

int main() {
   const auto options = forge::net::stcp::client_options{};
   return options.read_chunk_size == 0 ? 1 : 0;
}

import forge.net.transport.endpoint;

int main() {
   const auto endpoint = forge::net::transport::endpoint{.host = "127.0.0.1", .port = 9000};
   return endpoint.port == 9000 ? 0 : 1;
}

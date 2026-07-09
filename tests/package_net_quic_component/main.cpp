import forge.net.quic.transport;

int main() {
   const auto limits = forge::net::quic::from_transport_limits({});
   return limits.max_connections == 0 ? 1 : 0;
}

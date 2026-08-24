import forge.tooling.testing.host;

int main() {
   const auto host = forge::tooling::testing::host{};
   return host.state().accounts.empty() ? 0 : 1;
}

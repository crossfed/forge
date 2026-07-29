import forge.contract.testing.host;

int main() {
   const auto host = forge::contract::testing::host{};
   return host.state().accounts.empty() ? 0 : 1;
}

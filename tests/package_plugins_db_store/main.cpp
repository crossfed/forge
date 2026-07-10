import forge.plugins.db.store.plugin;

int main() {
   const auto descriptor = forge::plugins::db::store::descriptor();
   return descriptor.id.value == "forge.plugins.db.store" ? 0 : 1;
}

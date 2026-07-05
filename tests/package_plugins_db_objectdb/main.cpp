import forge.plugins.db.objectdb.plugin;

int main() {
   const auto descriptor = forge::plugins::db::objectdb::descriptor();
   return descriptor.id.value == "forge.plugins.db.objectdb" ? 0 : 1;
}

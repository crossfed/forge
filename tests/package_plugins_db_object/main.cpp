import forge.plugins.db.object.plugin;

int main() {
   const auto descriptor = forge::plugins::db::object::descriptor();
   return descriptor.id.value == "forge.plugins.db.object" ? 0 : 1;
}

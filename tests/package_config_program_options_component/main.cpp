import forge.config.core.component;
import forge.config.program_options;

int main() {
   auto registry = forge::config::core::component_registry{};
   auto text = forge::config::program_options::help(registry, "Options");
   return text.empty() ? 1 : 0;
}

import forge.config.core.document;
import forge.config.core.value;

int main() {
   auto document = forge::config::core::document{};
   document.set("service.enabled", forge::config::core::value{true});
   return document.try_get("service.enabled") ? 0 : 1;
}

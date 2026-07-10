import forge.codec.json;
import forge.variant.value;

int main() {
   auto written = forge::codec::json::write_value(forge::variant{true});
   return written.ok() && !written.text.empty() ? 0 : 1;
}

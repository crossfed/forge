import forge.codec.yaml;
import forge.variant.value;

int main() {
   auto written = forge::codec::yaml::write_value(forge::variant{true});
   return written.ok() && !written.text.empty() ? 0 : 1;
}

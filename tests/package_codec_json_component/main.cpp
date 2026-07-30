import forge.codec.json;
import forge.variant.value;
import package.codec.json.exact_types;

int main() {
   auto written = forge::codec::json::write_value(forge::variant{true});
   auto options = forge::codec::json::read_options{
       .described_records = forge::codec::json::described_record_policy::exact,
   };
   auto exact = forge::codec::json::read<package_codec_json::record>(R"({"value":7})", options);
   return written.ok() && !written.text.empty() && exact.ok() && exact.value.value == 7U ? 0 : 1;
}
